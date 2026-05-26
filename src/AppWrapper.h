/*
 * Authored by Alex Hultman, 2018-2026.
 * Modified by Daniel Dyryl, 2026.
 * Intellectual property of third-party.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Utilities.h"
extern thread_local int insideCorkCallback;


namespace AppWrapper {
  /* uWS.App.ws('/pattern', behavior) */
  template <typename APP>
  void ws(args_t args) {
  
      /* pattern, behavior */
      if (missingArguments(2, args)) {
          return;
      }
  
      Isolate *isolate = args.GetIsolate();
  
      Local<Context> context = isolate->GetCurrentContext();
  
      PerIsolateData *perIsolateData = (PerIsolateData *) Local<External>::Cast(args.Data())->Value();
  
      APP *app = (APP *) getInternalPointer(args.This());
      /* This one is default constructed with defaults */
      typename APP::template WebSocketBehavior<PerSocketData> behavior = {};
  
      NativeString pattern(args.GetIsolate(), args[0]);
      if (pattern.isInvalid(args)) {
          return;
      }
  
      // not UniquePersistent<> because that was an alias to Global<> for historical reasons
      Global<Function> upgradePf;
      Global<Function> openPf;
      Global<Function> messagePf;
      Global<Function> drainPf;
      Global<Function> closePf;
      Global<Function> droppedPf;
      Global<Function> pingPf;
      Global<Function> pongPf;
      Global<Function> subscriptionPf;
  
      /* Get the behavior object */
      if (args.Length() == 2) {
          Local<Object> behaviorObject = Local<Object>::Cast(args[1]);
  
          /* helpers */
          auto isPropertyOK = [](MaybeLocal<Value> property) -> bool {
            return !property.IsEmpty() && !property.ToLocalChecked()->IsUndefined();
          };
          auto setStandardSettingOrDefault = [=, &behavior]( Local<Value> str) {
              MaybeLocal<Value> property = behaviorObject->Get(context, str);
              if (isPropertyOK(property)) {
                  behavior.maxPayloadLength = property.ToLocalChecked()->Int32Value(context).ToChecked();
              }
          };
          auto setGlobalFn = [context, isolate, behaviorObject]( Global<Function>& fn, Local<Value> str ){
            fn.Reset(isolate, Local<Function>::Cast( behaviorObject->Get(context, str).ToLocalChecked() )); 
          };
          auto string = [isolate]<size_t N>( const char (&str)[N] ) -> Local<Value> {
              return String::NewFromUtf8(isolate, str, NewStringType::kNormal, N-1).ToLocalChecked();
          };
          setStandardSettingOrDefault(string("maxPayloadLength"));
          setStandardSettingOrDefault(string("idleTimeout"));
          setStandardSettingOrDefault(string("closeOnBackpressureLimit"));
          setStandardSettingOrDefault(string("sendPingsAutomatically")); 
          setStandardSettingOrDefault(string("maxBackpressure"));
  
          { // unique settings - clear stack soon
            
            /* maxLifetime or default */
            MaybeLocal<Value> maybeMaxLifetime = behaviorObject->Get(context, string("maxLifetime"));
            if (isPropertyOK(maybeMaxLifetime)) {
                /* Cap at 239 to avoid modulo wraparound in uSockets (240 % 240 == 0 == current timestamp, causing immediate timeout), and ensure non-negative */
                behavior.maxLifetime = std::max(0, std::min<int>(
                      maybeMaxLifetime.ToLocalChecked()->Int32Value(context).ToChecked(), 239
                ));
            }
  
            /* Compression or default, map from 0, 1, 2 to disabled, shared, dedicated. This is actually the enum */
            MaybeLocal<Value> maybeCompression = behaviorObject->Get(context, string("compression"));
            if (isPropertyOK(maybeCompression)) {
                behavior.compression = (uWS::CompressOptions) maybeCompression.ToLocalChecked()->Int32Value(
                    context
                ).ToChecked();
            }
          }
  
          setGlobalFn(upgradePf, string("upgrade"));
          setGlobalFn(openPf, string("open"));
          setGlobalFn(messagePf, string("message"));
          setGlobalFn(drainPf, string("drain"));
          setGlobalFn(closePf, string("close"));
          setGlobalFn(droppedPf, string("dropped"));
          setGlobalFn(pingPf, string("ping"));
          setGlobalFn(pongPf, string("pong"));
          setGlobalFn(subscriptionPf, string("subscription"));
      }
  
      /* Upgrade handler is always optional */
      if (upgradePf != Undefined(isolate)) {
          behavior.upgrade = [upgradePf = std::move(upgradePf), perIsolateData](auto *res, auto *req, auto *context) {
              Isolate *isolate = perIsolateData->isolate;
              HandleScope hs(isolate);
  
              Local<Function> upgradeLf = Local<Function>::New(isolate, upgradePf);
              Local<Object> resObject = perIsolateData->resTemplate[getAppTypeIndex<APP>()].Get(isolate)->Clone();
              setInternalPointer(resObject, res);
  
              Local<Object> reqObject = perIsolateData->reqTemplate[std::is_same<APP, uWS::H3App>::value].Get(isolate)->Clone();
              setInternalPointer(resObject, req);
  
              Local<Value> argv[3] = {resObject, reqObject, External::New(isolate, (void *) context)};
              CallJS(isolate, upgradeLf, 3, argv);
  
              /* Properly invalidate req */
              setInternalPointer(reqObject, nullptr);
  
              /* µWS itself will terminate if not responded and not attached
              * onAborted handler, so we can assume it's done */
          };
      }
  
      /* Open handler is NOT optional for the wrapper */
      behavior.open = [openPf = std::move(openPf), perIsolateData](auto *ws) {
          Isolate *isolate = perIsolateData->isolate;
          HandleScope hs(isolate);
  
          /* Create a new websocket object */
          Local<Object> wsObject = perIsolateData->wsTemplate[getAppTypeIndex<APP>()].Get(isolate)->Clone();
          setInternalPointer(wsObject, ws);
  
          /* Retrieve temporary userData object */
          PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
  
          /* Copy entires from userData, only if we have it set (not the case for default constructor) */
          if (!perSocketData->socketPf.IsEmpty()) {
              /* socketPf points to a stack allocated UniquePersistent, or nullptr, at this point */
              Local<Object> userData = perSocketData->socketPf.Get(isolate);
              Local<Context> context = isolate->GetCurrentContext();
              /* Merge userData and wsObject; this code is exceedingly horrible */
              Local<Array> keys;
              if (userData->GetOwnPropertyNames(context).ToLocal(&keys)) {
                  for (int i = 0; i < keys->Length(); i++) {
                      Local<Value> key = keys->Get(context, i).ToLocalChecked();
                      wsObject->Set(
                          context,
                          key,
                          userData->Get(context, key).ToLocalChecked()
                      ).ToChecked();
                  }
              }
  
              /* Also copy symbol properties */
              Local<Array> symbols;
              if (userData->GetOwnPropertyNames(context, PropertyFilter::SKIP_STRINGS).ToLocal(&symbols)) {
                  for (int i = 0; i < symbols->Length(); i++) {
                    Local<Value> symbol = symbols->Get(context, i).ToLocalChecked();
                    wsObject->Set(context,
                        symbol,
                        userData->Get(context, symbol).ToLocalChecked()
                    ).ToChecked();
                  }
              }
          }
  
          /* Attach a new V8 object with pointer to us, to it */
          perSocketData->socketPf.Reset(isolate, wsObject);
  
          Local<Function> openLf = openPf.Get(isolate);
          if (!openLf->IsUndefined()) {
              Local<Value> argv = wsObject;
              CallJS(isolate, openLf, 1, &argv);
          }
      };
  
      /* Message handler is always optional */
      if (messagePf != Undefined(isolate)) {
          behavior.message = [messagePf = std::move(messagePf), isolate](auto *ws, std::string_view message, uWS::OpCode opCode) {
              HandleScope hs(isolate);
  
              Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer_New(isolate, (void *) message.data(), message.length());
  
              PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
              Local<Value> argv[3] = {Local<Object>::New(isolate, perSocketData->socketPf),
                                      messageArrayBuffer,
                                      Boolean::New(isolate, opCode == uWS::OpCode::BINARY)};
  
              CallJS(isolate, Local<Function>::New(isolate, messagePf), 3, argv);
  
              /* Important: we clear the ArrayBuffer to make sure it is not invalidly used after return */
              messageArrayBuffer->Detach();
          };
      }
  
      /* Dropped handler is always optional (similar to message) */
      if (droppedPf != Undefined(isolate)) {
          behavior.dropped = [droppedPf = std::move(droppedPf), isolate](auto *ws, std::string_view message, uWS::OpCode opCode) {
              HandleScope hs(isolate);
  
              Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer_New(isolate, (void *) message.data(), message.length());
  
              PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
              Local<Value> argv[3] = {Local<Object>::New(isolate, perSocketData->socketPf),
                                      messageArrayBuffer,
                                      Boolean::New(isolate, opCode == uWS::OpCode::BINARY)};
  
              CallJS(isolate, Local<Function>::New(isolate, droppedPf), 3, argv);
  
              /* Important: we clear the ArrayBuffer to make sure it is not invalidly used after return */
              messageArrayBuffer->Detach();
          };
      }
  
      /* Drain handler is always optional */
      if (drainPf != Undefined(isolate)) {
          behavior.drain = [drainPf = std::move(drainPf), isolate](auto *ws) {
              HandleScope hs(isolate);
  
              PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
              Local<Value> argv = Local<Object>::New(isolate, perSocketData->socketPf);
              CallJS(isolate, Local<Function>::New(isolate, drainPf), 1, &argv);
          };
      }
  
      /* Subscription handler is always optional */
      if (subscriptionPf != Undefined(isolate)) {
          behavior.subscription = [subscriptionPf = std::move(subscriptionPf), isolate](auto *ws, std::string_view topic, int newCount, int oldCount) {
              HandleScope hs(isolate);
  
              PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
              Local<Value> argv[4] = {Local<Object>::New(isolate, perSocketData->socketPf), ArrayBuffer_New(isolate, (void *) topic.data(), topic.length()), Integer::New(isolate, newCount), Integer::New(isolate, oldCount)};
              CallJS(isolate, Local<Function>::New(isolate, subscriptionPf), 4, argv);
          };
      }
  
      /* Ping handler is always optional */
      if (pingPf != Undefined(isolate)) {
          behavior.ping = [pingPf = std::move(pingPf), isolate](auto *ws, std::string_view message) {
              HandleScope hs(isolate);
  
              PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
              Local<Value> argv[2] = {Local<Object>::New(isolate, perSocketData->socketPf), ArrayBuffer_New(isolate, (void *) message.data(), message.length())};
              CallJS(isolate, Local<Function>::New(isolate, pingPf), 2, argv);
          };
      }
  
      /* Pong handler is always optional */
      if (pongPf != Undefined(isolate)) {
          behavior.pong = [pongPf = std::move(pongPf), isolate](auto *ws, std::string_view message) {
              HandleScope hs(isolate);
  
              PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
              Local<Value> argv[2] = {Local<Object>::New(isolate, perSocketData->socketPf), ArrayBuffer_New(isolate, (void *) message.data(), message.length())};
              CallJS(isolate, Local<Function>::New(isolate, pongPf), 2, argv);
          };
      }
  
      /* Close handler is NOT optional for the wrapper */
      behavior.close = [closePf = std::move(closePf), isolate](auto *ws, int code, std::string_view message) {
          HandleScope hs(isolate);
  
          Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer_New(isolate, (void *) message.data(), message.length());
          PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
          Local<Object> wsObject = Local<Object>::New(isolate, perSocketData->socketPf);
  
          /* Invalidate this wsObject */
          setInternalPointer(wsObject, nullptr);
  
          /* Only call close handler if we have one set */
          Local<Function> closeLf = Local<Function>::New(isolate, closePf);
          if (!closeLf->IsUndefined()) {
              Local<Value> argv[3] = {wsObject, Integer::New(isolate, code), messageArrayBuffer};
              CallJS(isolate, closeLf, 3, argv);
          }
  
          /* This should technically not be required */
          perSocketData->socketPf.Reset();
  
          /* Again, here we clear the buffer to avoid strange bugs */
          messageArrayBuffer->Detach();
      };
  
      app->template ws<PerSocketData>(std::string(pattern.getString()), std::move(behavior));
  
      /* Return this */
      args.GetReturnValue().Set(args.This());
  }
  
  /* This method wraps get, post and all http methods */
  template <typename APP, typename F>
  void handler(F f, args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      /* Pattern */
      NativeString pattern(args.GetIsolate(), args[0]);
      if (pattern.isInvalid(args)) {
          return;
      }
  
      /* If the handler is null */
      if (args[1]->IsNull()) {
          (app->*f)(std::string(pattern.getString()), nullptr);
          args.GetReturnValue().Set(args.This());
          return;
      }
  
      /* If the handler is String */
      if (args[1]->IsArrayBuffer()) {
          NativeString constantString(args.GetIsolate(), args[1]);
          if (constantString.isInvalid(args)) {
              return;
          }
  
          (app->*f)(std::string(pattern.getString()), [response = std::string(constantString.getString().data(), constantString.getString().length())](auto *res, auto *req) {
              
  
              if constexpr (!std::is_same<APP, uWS::H3App>::value) {
  
                  /* Parse the DeclarativeResponse */
                  std::string_view remainingInstructions(response.data(), response.length());
                  while (remainingInstructions.length()) {
                      switch(remainingInstructions[0]) {
                          case 0: {
                              /* opCode END */
                              uint16_t length;
                              memcpy(&length, remainingInstructions.data() + 1, 2);
                              remainingInstructions.remove_prefix(3); // Skip opCode and length bytes
                              
                              res->end(remainingInstructions.substr(0, length));
                              remainingInstructions.remove_prefix(length);
                          }
                          break;
                          case 1: {
                              /* opCode WRITE_HEADER */
                              uint8_t keyLength;
                              memcpy(&keyLength, remainingInstructions.data() + 1, 1);
                              remainingInstructions.remove_prefix(2); // Skip opCode and key length bytes
                              
                              std::string_view keyString(remainingInstructions.data(), keyLength);
                              remainingInstructions.remove_prefix(keyLength);
  
                              uint8_t valueLength;
                              memcpy(&valueLength, remainingInstructions.data(), 1);
                              remainingInstructions.remove_prefix(1); // Skip value length bytes
                              
                              std::string_view valueString(remainingInstructions.data(), valueLength);
                              remainingInstructions.remove_prefix(valueLength);
  
                              res->writeHeader(keyString, valueString);
                          }
                          break;
                          case 2: {
                              /* opCode WRITE_BODY */
                              remainingInstructions.remove_prefix(1); // Skip opCode
                              //res->writeBody();
                          }
                          break;
                          case 3: {
                              /* opCode WRITE_QUERY_VALUE */
                              uint8_t keyLength;
                              memcpy(&keyLength, remainingInstructions.data() + 1, 1);
                              remainingInstructions.remove_prefix(2); // Skip opCode and key length bytes
                              
                              std::string_view keyString(remainingInstructions.data(), keyLength);
                              remainingInstructions.remove_prefix(keyLength);
  
                              res->write(req->getQuery(keyString));
                          }
                          break;
                          case 4: {
                              /* opCode WRITE_HEADER_VALUE */
                              uint8_t keyLength;
                              memcpy(&keyLength, remainingInstructions.data() + 1, 1);
                              remainingInstructions.remove_prefix(2); // Skip opCode and key length bytes
                              
                              std::string_view keyString(remainingInstructions.data(), keyLength);
                              remainingInstructions.remove_prefix(keyLength);
  
                              res->write(req->getHeader(keyString));
                          }
                          break;
                          case 5: {
                              /* opCode WRITE */
                              uint16_t length;
                              memcpy(&length, remainingInstructions.data() + 1, 2);
                              remainingInstructions.remove_prefix(3); // Skip opCode and length bytes
                              
                              std::string_view valueString(remainingInstructions.data(), length);
                              remainingInstructions.remove_prefix(length);
  
                              res->write(valueString);
                          }
                          break;
                          case 6: {
                              /* opCode WRITE_PARAMETER_VALUE */
                              uint8_t keyLength;
                              memcpy(&keyLength, remainingInstructions.data() + 1, 1);
                              remainingInstructions.remove_prefix(2); // Skip opCode and key length bytes
                              
                              std::string_view keyString(remainingInstructions.data(), keyLength);
                              remainingInstructions.remove_prefix(keyLength);
  
                              res->write(req->getParameter(keyString));
                          }
                          break;
                          case 7: {
                              /* opCode WRITE_STATUS */
                              uint8_t statusLength;
                              memcpy(&statusLength, remainingInstructions.data() + 1, 1);
                              remainingInstructions.remove_prefix(2); // Skip opCode and status length bytes
                              
                              std::string_view statusString(remainingInstructions.data(), statusLength);
                              remainingInstructions.remove_prefix(statusLength);
  
                              res->writeStatus(statusString);
                          }
                          break;
                      }
                  }
  
              }
              
  
          });
  
          args.GetReturnValue().Set(args.This());
          return;
      }
  
      /* Handler */
      Callback checkedCallback(args.GetIsolate(), args[1]);
      if (checkedCallback.isInvalid(args)) {
          return;
      }
      Global<Function> cb = checkedCallback.getFunction();
  
      /* This function requires perIsolateData */
      PerIsolateData *perIsolateData = (PerIsolateData *) Local<External>::Cast(args.Data())->Value();
  
      (app->*f)(std::string(pattern.getString()), [cb = std::move(cb), perIsolateData](auto *res, auto *req) {
          Isolate *isolate = perIsolateData->isolate;
          HandleScope hs(isolate);
  
          Local<Object> resObject = perIsolateData->resTemplate[getAppTypeIndex<APP>()].Get(isolate)->Clone();
          setInternalPointer(resObject, res);
  
          Local<Object> reqObject = perIsolateData->reqTemplate[std::is_same<APP, uWS::H3App>::value].Get(isolate)->Clone();
          setInternalPointer(reqObject, req);
  
          Local<Value> argv[] = {resObject, reqObject};
          
          //TODO test
          //Local<Context> context = isolate->GetCurrentContext();
          //insideCorkCallback++;
          //static_cast<void>(cb.Get(isolate)->Call(context, context->Global(), 2, argv));
          //insideCorkCallback++;
          
          CallJS(isolate, cb.Get(isolate), 2, argv);
  
          /* Properly invalidate req */
          setInternalPointer(reqObject, nullptr);
  
          /* µWS itself will terminate if not responded and not attached
           * onAborted handler, so we can assume it's done */
      });
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void close(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      app->close();
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void listen_unix(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
  
      Isolate *isolate = args.GetIsolate();
  
      /* Require at least two arguments */
      if (missingArguments(2, args)) {
          return;
      }
  
      /* integer options is first (not implemented) */
  
      /* Callback is first */
      auto cb = [&args, isolate](auto *token) {
          /* Return a false boolean if listen failed */
          Local<Value> argv = token ? Local<Value>::Cast(External::New(isolate, token)) : Local<Value>::Cast(Boolean::New(isolate, false));
          /* Immediate call cannot be CallJS */
          Local<Function>::Cast(args[0])->Call(isolate->GetCurrentContext(), isolate->GetCurrentContext()->Global(), 1, &argv).IsEmpty();
      };
  
      /* Path is last */
      std::string path;
      NativeString h(isolate, args[args.Length() - 1]);
      if (h.isInvalid(args)) {
          return;
      }
      path = h.getString();
  
      app->listen(std::move(cb), path);
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void listen(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      /* Require at least two arguments */
      if (missingArguments(2, args)) {
          return;
      }
  
      /* Callback is last */
      auto cb = [&args, isolate](auto *token) {
          /* Return a false boolean if listen failed */
          Local<Value> argv = token ? Local<Value>::Cast(External::New(isolate, token)) : Local<Value>::Cast(Boolean::New(isolate, false));
          /* Immediate call cannot be CallJS */
          Local<Function>::Cast(args[args.Length() - 1])->Call(isolate->GetCurrentContext(), isolate->GetCurrentContext()->Global(), 1, &argv).IsEmpty();
      };
  
      /* Host is first, if present */
      std::string host;
      if (!args[0]->IsNumber()) {
          NativeString h(isolate, args[0]);
          if (h.isInvalid(args)) {
              return;
          }
          host = h.getString();
      }
  
      /* Port, options are in the middle, if present */
      std::vector<int> numbers;
      for (int i = std::min<int>(1, host.length()); i < args.Length() - 1; i++) {
          numbers.push_back(args[i]->Uint32Value(args.GetIsolate()->GetCurrentContext()).ToChecked());
      }
  
      /* We only use the most complete overload */
      app->listen(host, numbers.size() ? numbers[0] : 0,
                  numbers.size() > 1 ? numbers[1] : 0, std::move(cb));
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void filter(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      /* Handler */
      Callback checkedCallback(args.GetIsolate(), args[0]);
      if (checkedCallback.isInvalid(args)) {
          return;
      }
      Global<Function> cb = checkedCallback.getFunction();
  
      /* This function requires perIsolateData */
      PerIsolateData *perIsolateData = (PerIsolateData *) Local<External>::Cast(args.Data())->Value();
  
      app->filter([cb = std::move(cb), perIsolateData](auto *res, int count) {
          Isolate *isolate = perIsolateData->isolate;
          HandleScope hs(isolate);
  
          Local<Object> resObject = perIsolateData->resTemplate[getAppTypeIndex<APP>()].Get(isolate)->Clone();
          setInternalPointer(resObject, res);
  
          Local<Value> argv[] = {resObject, Local<Value>::Cast(Integer::New(isolate, count))};
          CallJS(isolate, cb.Get(isolate), 2, argv);
      });
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void domain(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      /* serverName */
      if (missingArguments(1, args)) {
          return;
      }
  
      NativeString serverName(isolate, args[0]);
      if (serverName.isInvalid(args)) {
          return;
      }
  
      app->domain(std::string(serverName.getString()));
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void publish(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      /* topic, message [isBinary, compress] */
      if (missingArguments(2, args)) {
          return;
      }
  
      NativeString topic(isolate, args[0]);
      if (topic.isInvalid(args)) {
          return;
      }
  
      NativeString message(isolate, args[1]);
      if (message.isInvalid(args)) {
          return;
      }
  
      bool ok = app->publish(topic.getString(), message.getString(), args[2]->BooleanValue(isolate) ? uWS::OpCode::BINARY : uWS::OpCode::TEXT, args[3]->BooleanValue(isolate));
  
      args.GetReturnValue().Set(Boolean::New(isolate, ok));
  }
  
  template <typename APP>
  void numSubscribers(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      /* topic */
      if (missingArguments(1, args)) {
          return;
      }
  
      NativeString topic(isolate, args[0]);
      if (topic.isInvalid(args)) {
          return;
      }
  
      args.GetReturnValue().Set(Integer::New(isolate, app->numSubscribers(topic.getString())));
  }
  
  /* This one modified per-thread static strings temporarily */
  std::pair<uWS::SocketContextOptions, bool> readOptionsObject(args_t args, int index) {
      Isolate *isolate = args.GetIsolate();
      /* Read the options object if any */
      uWS::SocketContextOptions options = {};
      thread_local std::string keyFileName, certFileName, passphrase, dhParamsFileName, caFileName, sslCiphers;
      if (args.Length() > index && !args[index]->IsUndefined() && !args[index]->IsNull()) {
  
          if (!args[index]->IsObject()) {
              args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Options must be an object.", NewStringType::kNormal).ToLocalChecked())));
              return {};
          }
  
          Local<Object> optionsObject = Local<Object>::Cast(args[index]);
  
          /* Key file name */
          NativeString keyFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "key_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
          if (keyFileNameValue.isInvalid(args)) {
              return {};
          }
          if (keyFileNameValue.getString().length()) {
              keyFileName = keyFileNameValue.getString();
              options.key_file_name = keyFileName.c_str();
          }
  
          /* Cert file name */
          NativeString certFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "cert_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
          if (certFileNameValue.isInvalid(args)) {
              return {};
          }
          if (certFileNameValue.getString().length()) {
              certFileName = certFileNameValue.getString();
              options.cert_file_name = certFileName.c_str();
          }
  
          /* Passphrase */
          NativeString passphraseValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "passphrase", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
          if (passphraseValue.isInvalid(args)) {
              return {};
          }
          if (passphraseValue.getString().length()) {
              passphrase = passphraseValue.getString();
              options.passphrase = passphrase.c_str();
          }
  
          /* DH params file name */
          NativeString dhParamsFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "dh_params_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
          if (dhParamsFileNameValue.isInvalid(args)) {
              return {};
          }
          if (dhParamsFileNameValue.getString().length()) {
              dhParamsFileName = dhParamsFileNameValue.getString();
              options.dh_params_file_name = dhParamsFileName.c_str();
          }
  
          /* CA file name */
          NativeString caFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "ca_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
          if (caFileNameValue.isInvalid(args)) {
              return {};
          }
          if (caFileNameValue.getString().length()) {
              caFileName = caFileNameValue.getString();
              options.ca_file_name = caFileName.c_str();
          }
  
          /* ssl_prefer_low_memory_usage */
          options.ssl_prefer_low_memory_usage = optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "ssl_prefer_low_memory_usage", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked()->BooleanValue(isolate);
  
          /* ssl_ciphers */
          NativeString sslCiphersValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "ssl_ciphers", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
          if (sslCiphersValue.isInvalid(args)) {
              return {};
          }
          if (sslCiphersValue.getString().length()) {
              sslCiphers = sslCiphersValue.getString();
              options.ssl_ciphers = sslCiphers.c_str();
          }
      }
  
      return {options, true};
  }
  
  template <typename APP>
  void adoptSocket(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      int32_t fd = args[0]->Int32Value(isolate->GetCurrentContext()).ToChecked();
  
      NativeString ip(isolate, args[1]);
      if (ip.isInvalid(args)) {
          return;
      }
  
      app->adoptSocket(fd, ip.getString());
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void removeChildApp(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      double descriptor = args[0]->NumberValue(isolate->GetCurrentContext()).ToChecked();
  
      APP *receivingApp;
      memcpy(&receivingApp, &descriptor, sizeof(receivingApp));
  
      app->removeChildApp(receivingApp);
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void addChildApp(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      double descriptor = args[0]->NumberValue(isolate->GetCurrentContext()).ToChecked();
  
  
      APP *receivingApp;// = (APP *) args[0]->ToObject(isolate->GetCurrentContext()).ToLocalChecked()->GetAlignedPointerFromInternalField(0);
  
      memcpy(&receivingApp, &descriptor, sizeof(receivingApp));
  
      /* Todo: check the class type of args[0] must match class type of args.This() */
      //if (args[0])
  
      //std::cout << "addChildApp: " << receivingApp << std::endl;
  
      app->addChildApp(receivingApp);
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void getDescriptor(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
  
      static_assert(sizeof(double) >= sizeof(app));
  
      //static thread_local std::unordered_set<Global<Object>> persistentApps;
  
      Global<Object> *persistentApp = new Global<Object>;
      persistentApp->Reset(args.GetIsolate(), args.This());
  
      //persistentApps.emplace(persistentApp);
  
      double descriptor = 0;
      memcpy(&descriptor, &app, sizeof(app));
  
      //std::cout << "getDescriptor: " << app << std::endl;
  
      //std::cout << "Loop: " << app->getLoop() << std::endl;
  
      args.GetReturnValue().Set(Number::New(isolate, descriptor));
  }
  
  template <typename APP>
  void addServerName(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
      NativeString hostnamePatternValue(isolate, args[0]);
      if (hostnamePatternValue.isInvalid(args)) {
          return;
      }
      std::string hostnamePattern;
      if (hostnamePatternValue.getString().length()) {
          hostnamePattern = hostnamePatternValue.getString();
      }
  
      auto [options, valid] = readOptionsObject(args, 1);
      if (!valid) {
          return;
      }
  
      app->addServerName(hostnamePattern.c_str(), options);
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void removeServerName(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
  
      Isolate *isolate = args.GetIsolate();
      NativeString hostnamePatternValue(isolate, args[0]);
      if (hostnamePatternValue.isInvalid(args)) {
          return;
      }
      std::string hostnamePattern;
      if (hostnamePatternValue.getString().length()) {
          hostnamePattern = hostnamePatternValue.getString();
      }
  
      app->removeServerName(hostnamePattern.c_str());
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void missingServerName(args_t args) {
      APP *app = (APP *) getInternalPointer(args.This());
      Isolate *isolate = args.GetIsolate();
  
      Global<Function> missingPf;
      missingPf.Reset(args.GetIsolate(), Local<Function>::Cast(args[0]));
  
      app->missingServerName([missingPf = std::move(missingPf), isolate](const char *hostname) {
          /* We hand a JavaScript string here */
          HandleScope hs(isolate);
          Local<Function> missingLf = Local<Function>::New(isolate, missingPf);
          Local<Value> argv[1] = {String::NewFromUtf8(isolate, hostname, NewStringType::kNormal).ToLocalChecked()};
          CallJS(isolate, missingLf, 1, argv);
      });
  
      args.GetReturnValue().Set(args.This());
  }
  
  template <typename APP>
  void init(args_t args) {
  
      Isolate *isolate = args.GetIsolate();
  
      Local<FunctionTemplate> appTemplate = FunctionTemplate::New(isolate);
      appTemplate->SetClassName(String::NewFromUtf8(isolate, std::is_same<APP, uWS::SSLApp>::value ? "uWS.SSLApp" : "uWS.App", NewStringType::kNormal).ToLocalChecked());
  
      auto [options, valid] = readOptionsObject(args, 0);
      if (!valid) {
          return;
      }
  
      APP *app;
  
      if constexpr (!std::is_same<APP, uWS::H3App>::value) {
  
          /* uSockets copies strings here */
          app = new APP(options);
  
          /* Throw if we failed to construct the app */
          if (app->constructorFailed()) {
              delete app;
              args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "App construction failed", NewStringType::kNormal).ToLocalChecked())));
              return;
          }
  
      } else {
  
          appTemplate->SetClassName(String::NewFromUtf8(isolate, "uWS.H3App", NewStringType::kNormal).ToLocalChecked());
  
          app = new APP(options);
      }
  
      appTemplate->InstanceTemplate()->SetInternalFieldCount(1);
      
      Local<ObjectTemplate> appObjectTemplate = appTemplate->PrototypeTemplate();
      Local<Value> settings = args.Data();
  
      /* helper */
      auto regFn = [appObjectTemplate, isolate, settings]<size_t N>(
        const char (&str)[N],
        void(*cb)(args_t)
      ){
        appObjectTemplate->Set(
          String::NewFromUtf8(isolate, str, NewStringType::kNormal, N-1).ToLocalChecked(),
          FunctionTemplate::New(isolate, cb, settings)
        );
      };
  
      /* All the http methods */
      regFn("get", [](auto &args) {
          
          /* Add non-cached variants */
          if constexpr (std::is_same<APP, uWS::App>::value) {
  
              if (args.Length() == 3) {
                  /* Use cached variant */
                  std::cout << "Registering cached get handler" << std::endl;
  
  
                  APP *app = (APP *) getInternalPointer(args.This());
  
                  /* Pattern */
                  NativeString pattern(args.GetIsolate(), args[0]);
                  if (pattern.isInvalid(args)) {
                      return;
                  }
  
                  /* Handler */
                  Callback checkedCallback(args.GetIsolate(), args[1]);
                  if (checkedCallback.isInvalid(args)) {
                      return;
                  }
                  Global<Function> cb = checkedCallback.getFunction();
  
                  /* This function requires perIsolateData */
                  PerIsolateData *perIsolateData = (PerIsolateData *) Local<External>::Cast(args.Data())->Value();
  
                  app->get(std::string(pattern.getString()), [cb = std::move(cb), perIsolateData](auto *res, auto *req) {
                      Isolate *isolate = perIsolateData->isolate;
                      HandleScope hs(isolate);
  
  
                      // this needs to be cachedresponse wrapper (for both cached tcp and cached SSL?)
                      Local<Object> resObject = perIsolateData->resTemplate[/*getAppTypeIndex<APP>()*/3].Get(isolate)->Clone();
                      setInternalPointer(resObject, res);
  
                      Local<Object> reqObject = perIsolateData->reqTemplate[std::is_same<APP, uWS::H3App>::value].Get(isolate)->Clone();
                      setInternalPointer(reqObject, req);
  
                      Local<Value> argv[] = {resObject, reqObject};
                      CallJS(isolate, cb.Get(isolate), 2, argv);
  
                      /* Properly invalidate req */
                      setInternalPointer(reqObject, nullptr);
  
                      /* µWS itself will terminate if not responded and not attached
                      * onAborted handler, so we can assume it's done */
                  }/*, 13*/);
  
                  args.GetReturnValue().Set(args.This());
  
  
              } else {
                  handler<APP>(&uWS::TemplatedApp<false>::get, args);
              }
  
          } else if constexpr (std::is_same<APP, uWS::SSLApp>::value) {
              handler<APP>(&uWS::TemplatedApp<true>::get, args);
          }
         
      });
  
      regFn("post", [](args_t args) {
          handler<APP>(&APP::post, args);
      });
  
      regFn("options", [](args_t args) {
          handler<APP>(&APP::options, args);
      });
  
      regFn("del", [](args_t args) {
          handler<APP>(&APP::del, args);
      });
  
      regFn("patch", [](args_t args) {
          handler<APP>(&APP::patch, args);
      });
  
      regFn("put", [](args_t args) {
          handler<APP>(&APP::put, args);
      });
  
      regFn("head", [](args_t args) {
          handler<APP>(&APP::head, args);
      });
  
      regFn("connect", [](args_t args) {
          handler<APP>(&APP::connect, args);
      });
  
      regFn("trace", [](args_t args) {
          handler<APP>(&APP::trace, args);
      });
  
      /* Any http method */
      regFn("any", [](args_t args) {
          handler<APP>(&APP::any, args);
      });
  
      regFn("listen", listen<APP>);
      
      if constexpr (!std::is_same<APP, uWS::H3App>::value) {
          /* helper */
          regFn("close", close<APP>);
          regFn("listen_unix", listen_unix<APP>);
          regFn("filter", filter<APP>);
  
          /* load balancing */
          regFn("removeChildAppDescriptor", removeChildApp<APP>);
          regFn("addChildAppDescriptor", addChildApp<APP>);
          regFn("getDescriptor", getDescriptor<APP>);
          regFn("adoptSocket", adoptSocket<APP>);
  
          /* ws, listen */
          regFn("ws", ws<APP>);
          regFn("publish", publish<APP>);
          regFn("numSubscribers", numSubscribers<APP>);
  
          regFn("domain", domain<APP>);
  
          /* SNI */
          regFn("addServerName", addServerName<APP>);
          regFn("removeServerName", removeServerName<APP>);
          regFn("missingServerName", missingServerName<APP>);
  
      }
  
      Local<Object> localApp = appTemplate->GetFunction(isolate->GetCurrentContext()).ToLocalChecked()->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
      setInternalPointer(localApp, app);
  
      PerIsolateData *perIsolateData = (PerIsolateData *) Local<External>::Cast(args.Data())->Value();
  
  
      if constexpr (!std::is_same<APP, uWS::H3App>::value) {
  
          /* Add this to our delete list */
          if constexpr (std::is_same<APP, uWS::SSLApp>::value) {
              perIsolateData->sslApps.emplace_back(app);
          } else {
              perIsolateData->apps.emplace_back(app);
          }
  
      }
  
      args.GetReturnValue().Set(localApp);
  }
  
}

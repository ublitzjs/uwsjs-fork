
#ifndef ADDON_UTILITIES_H
#define ADDON_UTILITIES_H
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "App.h"
#include "Http3App.h"

#include <v8.h>
#include <v8-fast-api-calls.h>

/* Unfortunately we _have_ to depend on Node.js crap */
#include <node.h>

using namespace v8;

// helper to make life easier
using args_t = const FunctionCallbackInfo<Value>&;

/* Getting internal pointer is different in recent V8 versions */
  void *getInternalPointer(const Local<Object> &holder) {
    return holder->GetAlignedPointerFromInternalField(
#if (V8_MAJOR_VERSION == 14)
        0, 0
#else
       0
#endif
    );
  }
  void setInternalPointer(const Local<Object> &holder, void *value) {
    holder->SetAlignedPointerInInternalField(
#if (V8_MAJOR_VERSION == 14)
        0, value, 0
#else
       0, value
#endif
    );
  }

// These enums constraint "template" params for functions. 
namespace OPTIONS {
  enum class ENUM : uint32_t {
    TCP, SSL, QUIC, CACHE 
  };
  template <ENUM Option>
  constexpr void IS_TCP_OR_QUIC() {
    static_assert(Option == ENUM::TCP || Option == ENUM::QUIC, "Given option is neither TCP nor QUIC");
  }
  template <ENUM Option> 
  constexpr void IS_TCP_OR_SSL() {
    static_assert(Option == ENUM::TCP || Option == ENUM::SSL, "Given option is neither TCP nor SSL");
  }
}

MaybeLocal<Value> CallJS(Isolate *isolate, Local<Function> f, int argc, Local<Value> *argv) {
    extern int calledIntoJS;
    extern thread_local int insideCorkCallback;
    /* All calls we do into JS are properly corked, except for res.cork, where we increase the counter explicitly */
    insideCorkCallback++;
    /* Slow path */
    auto ret = node::MakeCallback(isolate, isolate->GetCurrentContext()->Global(), f, argc, argv, {0, 0});
    insideCorkCallback--;
    return ret;
}

Local<v8::ArrayBuffer> ArrayBuffer_New(Isolate *isolate, void *data, size_t length) {
    std::unique_ptr<BackingStore> backingStore = ArrayBuffer::NewBackingStore(data, length, [](void* data, size_t length, void* deleter_data) {}, nullptr);
    return ArrayBuffer::New(isolate, std::shared_ptr<BackingStore>(backingStore.release()));
}

Local<v8::ArrayBuffer> ArrayBuffer_NewCopy(Isolate *isolate, void *data, size_t length) {
    Local<ArrayBuffer> ab = ArrayBuffer::New(isolate, length);
    memcpy(ab->GetBackingStore()->Data(), data, length);
    return ab;
}


/* uWebSockets requires a struct as "userData" for WebSockets -> here it is */
struct PerSocketData {
    Global<Object> socketPf;
};

/** 
 *  Global struct that is generated once per Isolate (V8 instance for one thread). 
 *  Ergonomic because of automatical destruction of Global<> properties
 * */
struct PerIsolateData {
    Isolate *isolate;

    Global<Object> reqTemplate[2]; // 0 = non-SSL/SSL, 1 = Http3
    Global<Object> resTemplate[4]; // 0 = non-SSL, 1 = SSL, 2 = Http3
    Global<Object> wsTemplate[2];

    /* We hold all apps until free */
    std::vector<std::unique_ptr<uWS::App>> apps;
    std::vector<std::unique_ptr<uWS::SSLApp>> sslApps;
};

/* just a getter method for PROTOCOLS enum, based on the APP instance */
template <class APP>
static constexpr uint32_t getAppTypeIndex() {
    //return std::is_same<APP, uWS::SSLApp>::value;

    if constexpr (std::is_same<APP, uWS::App>::value) {
        return static_cast<uint32_t>(OPTIONS::ENUM::TCP);
    } else if constexpr (std::is_same<APP, uWS::SSLApp>::value) {
        return static_cast<uint32_t>(OPTIONS::ENUM::SSL);
    } else if constexpr (std::is_same<APP, uWS::H3App>::value) {
        return static_cast<uint32_t>(OPTIONS::ENUM::QUIC);
    } else {
        // why does this fail?
        //static_assert(false);
    }
}

static inline bool missingArguments(int length, args_t args) {
    if (args.Length() < length) {
        std::string message = "Function requires at least ";
        message += std::to_string(length);
        message += " arguments.";
        args.GetReturnValue().Set(args.GetIsolate()->ThrowException(v8::Exception::Error(String::NewFromUtf8(args.GetIsolate(), message.c_str(), NewStringType::kNormal).ToLocalChecked())));
        return true;
    }
    return false;
}

struct Callback {
    bool invalid = false;
    Global<Function> f;
    Callback(Isolate *isolate, const Local<Value> &value) {

        if (!value->IsFunction()) {
            invalid = true;
            return;
        }

        f.Reset(isolate, Local<Function>::Cast(value));
    }

    bool isInvalid(args_t args) {
        if (invalid) {
            args.GetReturnValue().Set(args.GetIsolate()->ThrowException(v8::Exception::Error(String::NewFromUtf8(args.GetIsolate(), "Passed callback is not a valid function.", NewStringType::kNormal).ToLocalChecked())));
        }
        return invalid;
    }

    Global<Function> &&getFunction() {
        return std::move(f);
    }
};

template <bool AllowStringView = false>
class NativeString {
    char *data;
    size_t length;
    bool allocated = false;
    bool invalid = false;

    // Static thread-local state shared by all NativeString instances on this thread
    inline static thread_local std::vector<char> pool = std::vector<char>(128 * 1024);
    inline static thread_local size_t pool_offset = 0;
    inline static thread_local int ref_count = 0;

    static char* alloc(size_t size) {
        // Ensure size is a multiple of 8
        size = (size + 7) & ~7;

        // Fallback for allocations larger than the remaining pool space
        if (pool_offset + size > pool.size()) {
            // Mark for external cleanup if using instance-based logic
            // (Note: In a pure static alloc, you'd need a way to track this)
            return (char*)std::malloc(size);
        }

        char* ptr = pool.data() + pool_offset;
        pool_offset += size;
        return ptr;
    }

    // Provided for completeness, though the "pool" doesn't actually free individual slices
    static void free(char* ptr) {
        if (ptr < pool.data() || ptr >= pool.data() + pool.size()) {
            ::free(ptr);
        }
    }

public:
    NativeString(Isolate *isolate, const Local<Value> &value) {
        if (ref_count == 0) {
            pool_offset = 0; // Reset the "stack" when entering the first scope
        }
        ref_count++;

        if (value->IsUndefined()) {
            data = nullptr;
            length = 0;
        } else if (value->IsString()) {
            Local<String> string = Local<String>::Cast(value);

            /* StringView path is Latin-1, not Utf-8 */

            // Fallback
#if (V8_MAJOR_VERSION == 14) 
            length = string->Utf8LengthV2(isolate);
            data = alloc(length);
            allocated = true;
            string->WriteUtf8V2(isolate, data, length);
#else
            length = string->Utf8Length(isolate);
            data = alloc(length);
            allocated = true;
            string->WriteUtf8(isolate, data, length, nullptr, String::WriteOptions::NO_NULL_TERMINATION);
#endif
        } else if (value->IsArrayBufferView()) {
            Local<ArrayBufferView> arrayBufferView = Local<ArrayBufferView>::Cast(value);
            auto contents = arrayBufferView->Buffer()->GetBackingStore();
            length = arrayBufferView->ByteLength();
            data = (char *) contents->Data() + arrayBufferView->ByteOffset();
        } else if (value->IsArrayBuffer()) {
            Local<ArrayBuffer> arrayBuffer = Local<ArrayBuffer>::Cast(value);
            auto contents = arrayBuffer->GetBackingStore();
            length = contents->ByteLength();
            data = (char *) contents->Data();
        } else if (value->IsSharedArrayBuffer()) {
            Local<SharedArrayBuffer> arrayBuffer = Local<SharedArrayBuffer>::Cast(value);
            auto contents = arrayBuffer->GetBackingStore();
            length = contents->ByteLength();
            data = (char *) contents->Data();
        } else {
            invalid = true;
        }
    }

    bool isInvalid(args_t args) {
        if (invalid) {
            args.GetReturnValue().Set(args.GetIsolate()->ThrowException(v8::Exception::Error(String::NewFromUtf8(args.GetIsolate(), "Text and data can only be passed by String, ArrayBuffer or ArrayBufferView.", NewStringType::kNormal).ToLocalChecked())));
        }
        return invalid;
    }

    std::string_view getString() {
        return {data, length};
    }

    ~NativeString() {
        ref_count--;
        if (allocated) {
            free(data);
        }
    }
};

// Utility function to extract raw certificate data
std::string extractX509PemCertificate(SSL* ssl) {
    std::string pemCertificate;

    if (!ssl) {
        return pemCertificate;
    }

    // Get the peer certificate
    X509* peerCertificate = SSL_get_peer_certificate(ssl);
    if (!peerCertificate) {
        // No peer certificate available
        return pemCertificate;
    }

    // Convert X509 certificate to PEM format
    BIO* bio = BIO_new(BIO_s_mem());
    if(bio) {
        if (PEM_write_bio_X509(bio, peerCertificate)) {
            char* buffer;
            long length = BIO_get_mem_data(bio, &buffer);
            pemCertificate.assign(buffer, length);
        }
        BIO_free(bio);
    }

    // Free the peer certificate
    X509_free(peerCertificate);
    return pemCertificate;
}

#endif

# uwsjs-fork precompiled AddressSanitizer binaries for UNIX systems
Automatically built from source by GitHub Actions. 
## How to use?
LD_PRELOAD=$(gcc -print-file-name=libasan.so) node server.js
## Clarification of license
Files in this "binaries" branch are all licensed under Apache License 2.0, despite some of them lacking a notice as per "APPENDIX: How to apply the Apache License to your work" of the Apache License 2.0.

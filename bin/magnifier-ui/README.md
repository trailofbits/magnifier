# MagnifierUI

This is a Web UI built to provide a more user-friendly interface for Magnifier.
It consists of a Vue.js frontend and a C++ backend, and it uses multi-session websockets to facilitate communication between the two.
The MagnifierUI not only exposes most of the features Magnifier has to offer, but it also integrates [rellic](https://github.com/lifting-bits/rellic), the LLVM IR to C code decompiler, to show side-by-side C code decompilation results.

## Building

To build the C++ backend server, the `MAGNIFIER_ENABLE_WEBUI` option needs to be enabled in cmake. The `web-ui` target can then be compiled and executed.

The Vue.js frontend relies on `node.js` and `npm` for the build process. The following commands can be used to build for development/release:


```bash
# install dependencies
$ npm install

# serve with hot reload for development
$ npm run dev

# build for production and launch server
$ npm run build
$ npm run start

# generate static project
$ npm run generate
```
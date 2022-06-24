# MagnifierUI

This is a Web UI built to provide a more user-friendly interface for Magnifier.
It consists of a Vue.js frontend and a C++ backend, and it uses multi-session websockets to facilitate communication between the two.
The MagnifierUI not only exposes most of the features Magnifier has to offer, but it also integrates [rellic](https://github.com/lifting-bits/rellic), the LLVM IR to C code decompiler, to show side-by-side C code decompilation results.

## Building

### Backend

To build the C++ backend server, the `MAGNIFIER_ENABLE_UI` option needs to be enabled in CMake.
This can be achieved by either adding `-DMAGNIFIER_ENABLE_UI=ON` to the CMake command line or directly editing the `CMakeList.txt` file.

The backend server also depends on `uwebsockets`, `usockets`, and [rellic](https://github.com/lifting-bits/rellic).
Both `uwebsockets` and `usockets` can be installed with vcpkg.

The `magnifier-ui` target can then be compiled and executed. It will expose a websocket server on port 9001 by default.

### Frontend

The Vue.js frontend relies on `node.js` and `npm` for the build process.
It communicates with the C++ backend and displays the correct visuals.
By default, it will try to connect to the websocket on `localhost:9001/ws`.
This can be changed by editing the `www/config.js` file.

During development, quick code reload is a very nice feature to have.
To start a development server, the following commands can be used:

```bash
# enter the www/ directory
$ cd www/

# install dependencies
$ npm install

# serve with hot reload for development
$ npm run dev
```

When it's time to deploy the project, a full static build can be generated:

```bash
# enter the www/ directory
$ cd www/

# install dependencies
$ npm install

# generate static project
$ npm run generate
```

The content generated in `www/dist/` can then be copied and served just like any other static page websites (through something like github pages) while retaining its full functionality.
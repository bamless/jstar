/**
 * MIT License
 *
 * Copyright (c) 2025 Fabrizio Pietrucci
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// =============================================================================
// WASM entry point for the documentation interactive demo
// =============================================================================
//
// This file is compiled with Emscripten and linked against jstar_static to
// produce the jstar.js / jstar.wasm pair that powers the in-browser J* REPL
// on the docs site.
//
// The JavaScript side (jstar-api.js) grabs `jstar_run` via Emscripten's
// cwrap() and routes C stdout / stderr through the Module.print and
// Module.printErr callbacks configured there.

#include <emscripten.h>

#include "jstar/jstar.h"

// Evaluate `src` in a fresh, short-lived VM and return the raw JStarResult
// code (0 = JSR_SUCCESS; see JStarResult in jstar.h for the other values).
//
// A new VM is created for every call so that state cannot leak between runs
// in the interactive demo.  The default conf uses jsrPrintErrorCB, which
// writes syntax/compile errors to stderr — Emscripten maps that stream to
// Module.printErr as set up by jstar-api.js.
EMSCRIPTEN_KEEPALIVE
int jstar_run(const char* src) {
    JStarConf conf = jsrGetConf();
    JStarVM* vm = jsrNewVM(&conf);
    jsrInitRuntime(vm);
    JStarResult res = jsrEvalString(vm, "<docs>", src);
    jsrFreeVM(vm);
    return (int)res;
}
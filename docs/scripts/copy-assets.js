#!/usr/bin/env node
'use strict';

// ==============================================================================
// Copy and minify npm dependencies into assets/
//
// Run via `npm run copy-assets` from the docs/ directory, or automatically
// on `npm install` through the postinstall hook.
// ==============================================================================

const fs     = require('fs');
const path   = require('path');
const esbuild = require('esbuild');

// Resolve paths relative to docs/ (one level up from this script).
const docs = path.resolve(__dirname, '..');
const nm   = path.join(docs, 'node_modules');

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Bundle + minify a JS entry point with esbuild.
async function buildJS(entryPoint, outfile, opts = {}) {
    await esbuild.build({
        entryPoints: [entryPoint],
        bundle:      opts.bundle ?? false,
        minify:      true,
        outfile:     path.join(docs, outfile),
        logLevel:    'silent',
        ...opts,
    });
    const tag = opts.bundle ? '(bundle)' : '(min)';
    console.log(`  ${tag} ${path.relative(docs, entryPoint)} → ${outfile}`);
}

// Minify a CSS file with esbuild.
async function buildCSS(src, dest) {
    await esbuild.build({
        entryPoints: [path.join(nm, src)],
        bundle:      false,
        minify:      true,
        outfile:     path.join(docs, dest),
        logLevel:    'silent',
    });
    console.log(`  (min) ${src} → ${dest}`);
}

// Copy a file unchanged (e.g. already-minified Font Awesome CSS).
function copy(src, dest) {
    const destFull = path.join(docs, dest);
    fs.mkdirSync(path.dirname(destFull), { recursive: true });
    fs.copyFileSync(path.join(nm, src), destFull);
    console.log(`  (cp)  ${src} → ${dest}`);
}

// Copy all regular files from a directory unchanged.
function copyDir(src, dest) {
    const srcFull  = path.join(nm, src);
    const destFull = path.join(docs, dest);
    fs.mkdirSync(destFull, { recursive: true });
    for (const file of fs.readdirSync(srcFull)) {
        const s = path.join(srcFull, file);
        if (fs.statSync(s).isFile()) {
            fs.copyFileSync(s, path.join(destFull, file));
        }
    }
    console.log(`  (cp)  ${src}/ → ${dest}/`);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
    console.log('Copying npm dependencies to assets/...\n');

    // PrismJS — minify the pre-bundled script and both CSS themes.
    await buildJS(path.join(nm, 'prismjs/prism.js'), 'assets/js/prism.js');
    await buildCSS('prismjs/themes/prism.css',         'assets/css/prism.css');
    await buildCSS('prismjs/themes/prism-okaidia.css', 'assets/css/prism-dark.css');

    // CodeJar v4 uses ES modules; bundle it into an IIFE so it works as a
    // plain <script> tag and exposes CodeJar as a window global.
    await esbuild.build({
        stdin: {
            contents:   `import { CodeJar } from 'codejar'; window.CodeJar = CodeJar;`,
            resolveDir: docs,
        },
        bundle:   true,
        minify:   true,
        outfile:  path.join(docs, 'assets/js/codejar.js'),
        logLevel: 'silent',
    });
    console.log('  (bundle) codejar → assets/js/codejar.js');

    // Font Awesome — CSS is pre-minified; copy CSS and webfonts unchanged.
    copy('@fortawesome/fontawesome-free/css/fontawesome.min.css', 'assets/css/fontawesome.min.css');
    copy('@fortawesome/fontawesome-free/css/brands.min.css',      'assets/css/brands.min.css');
    copy('@fortawesome/fontawesome-free/css/solid.min.css',       'assets/css/solid.min.css');
    copyDir('@fortawesome/fontawesome-free/webfonts',             'assets/webfonts');

    console.log('\nDone.');
}

main().catch(err => { console.error(err); process.exit(1); });

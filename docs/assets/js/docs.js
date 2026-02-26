// Main script that gets included in all pages of the site.
// It mainly processes `runnable-snippets` elements and sets up the logic to execute them.

window.addEventListener('load', () => {
    (function () {
        const escapeString = (str) => {
            return str.replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/"/g, "&quot;")
                .replace(/'/g, "&#039;");
        }

        const runSnippet = (snippetBlock, code) => {
            let resultBlock = snippetBlock.querySelector('.snippet-output');
            if (!resultBlock) {
                resultBlock = document.createElement('pre');
                resultBlock.className = 'snippet-style snippet-output';
                snippetBlock.appendChild(resultBlock);
            }

            jstar_api.run(code);

            resultBlock.innerText = jstar_api.out;
            if (jstar_api.err.length > 0) {
                const error = `<span class='snippet-error'>${escapeString(jstar_api.err)}</span>`;
                resultBlock.innerHTML += error;
            }
        }

        // Set up all interactive snippets
        Array.from(document.querySelectorAll('.runnable-snippet')).forEach((snippetBlock) => {
            const sourceCode = snippetBlock.innerText;

            // Wrap all the snippet elements in a div
            const snippetWrap = document.createElement('div');
            snippetWrap.className = 'snippet';
            snippetBlock.replaceWith(snippetWrap);
            snippetBlock = snippetWrap;

            const pre = document.createElement('pre');
            pre.className = 'snippet-editor language-jstar';

            snippetWrap.appendChild(pre);

            // Setup the editor
            const editor = CodeJar(pre, (el) => Prism.highlightElement(el));
            editor.updateCode(sourceCode);

            // Setup the buttons that control the code in the editor block
            const buttons = document.createElement('div');
            buttons.className = 'snippet-buttons';
            snippetBlock.appendChild(buttons);

            const runButton = document.createElement('button');
            runButton.className = 'fas fa-play snippet-button';
            runButton.hidden = true;
            runButton.title = 'Run the code';
            runButton.setAttribute('aria-label', runButton.title);
            runButton.addEventListener('click', () => runSnippet(snippetBlock, editor.toString()));
            buttons.appendChild(runButton);

            const resetButton = document.createElement('button');
            resetButton.className = 'fas fa-history snippet-button';
            resetButton.hidden = true;
            resetButton.title = 'Undo changes';
            runButton.setAttribute('aria-label', resetButton.title);
            resetButton.addEventListener('click', () => {
                const outputBlock = snippetBlock.querySelector('.snippet-output');
                if (outputBlock) outputBlock.remove();
                editor.updateCode(sourceCode);
            });
            buttons.appendChild(resetButton);
        });
    })();

});

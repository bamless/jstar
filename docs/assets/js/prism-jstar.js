// J* language definition for PrismJS.
// Must be loaded after prism.js.
Prism.languages.jstar = {
    'comment': [
        { pattern: /(^|[^\\])\/\*[\s\S]*?(?:\*\/|$)/, lookbehind: true },
        { pattern: /(^|[^\\:])\/\/.*/, lookbehind: true, greedy: true }
    ],
    'string': {
        pattern: /(?:[rub]|rb|br)?("|')[\s\S]*?\1/i,
        greedy: true
    },
    'class-name': {
        pattern: /(\b(?:class)\s+|\bcatch\s+\()[\w.\\]+/i,
        lookbehind: true,
        inside: { 'punctuation': /[.\\]/ }
    },
    'number': /\b0x[a-f\d]+\.?[a-f\d]*(?:p[+-]?\d+)?\b|\b\d+(?:\.\B|\.?\d*(?:e[+-]?\d+)?\b)|\B\.\d+(?:e[+-]?\d+)?\b/i,
    'keyword': /\b(?:class|construct|yield|this|super|and|else|for|fun|native|if|elif|or|return|var|while|import|in|begin|end|as|is|try|except|ensure|raise|with|continue|break|static)\b/,
    'boolean': /\b(?:true|false|null)\b/,
    'function': /(?!\d)\w+(?=\s*(?:[({]))/,
    'operator': [/[-+*%^#~&|]|\/\/?|<[<=]?|>[>=]?|[=~]=?/],
    'punctuation': /[\[\](){},;]|\.+|:+/
};
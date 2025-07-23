" Vim syntax file
" Language: jstar
" Maintainer: bamless
" Filename: jstar.vim

" Install:
" 1. Copy this file in ~/.vim/syntax/jstar.vim
" 2. Add this to your .vimrc:
" autocmd BufRead,BufNewFile *.jsr set filetype=jstar

if exists("b:current_syntax")
  finish
endif

syntax case match

" --- Keywords ---
syntax keyword jstarKeyword and or fun construct native var static yield
syntax keyword jstarKeyword return if elif else while for break continue in
syntax keyword jstarKeyword true false null super this
syntax keyword jstarKeyword import as try ensure except raise with begin end is
syntax keyword jstarKeyword class

" --- Conditional keywords ---
syntax keyword jstarConditional if elif else try except ensure raise end

" --- Repeat (loop) keywords ---
syntax keyword jstarRepeat for while break continue end

" --- Exception keywords ---
syntax keyword jstarException raise

" --- Include/Import ---
syntax keyword jstarInclude import

" --- Functions ---
syntax match jstarFunction /^\s*fun\s\+\zs\k\+\ze\s*(/ contains=jstarFunctionName
syntax match jstarFunctionName /\<[a-z][a-zA-Z0-9_]*\>/

" --- Classes ---
syntax match jstarClass /^\s*class\s\+\zs\k\+/ contains=jstarClassName
syntax match jstarClassName /\<[A-Z][a-zA-Z0-9_]*\>/

" --- Function calls ---
syntax match jstarFunctionCall /\<\k\+\ze\s*(/ contains=jstarClassName

" --- Constants: ALL CAPS with optional underscores/digits ---
syntax match jstarConstant /\<[A-Z][A-Z0-9_]*\>/

" --- Strings ---
syntax region jstarString start=+"+ skip=+\\\\\|\\"+ end=+"+
syntax region jstarString start=+'+ skip=+\\\\\\|\\'+ end=+'+

" --- Comments ---
syntax region jstarComment start="//" end="$" contains=jstarTodo
syntax keyword jstarTodo TODO XXX FIXME NOTE

" --- Decorators ---
syntax match jstarDecorator /@\k\+/

" --- Shabang ---
syntax match jstarShabang /^#!.*$/

" --- Numbers ---
syntax match jstarNumber /\v0x[a-fA-F0-9]+|\d+(\.\d+)?([eE][+-]?\d+)?/

" --- Operators ---
syntax match jstarOperator /[-+*\/%!=<>^|&~]+/

" --- Punctuation/Delimiters ---
syntax match jstarDelimiter /[(){}\[\],;:.@]+/

" --- Linking to Vim's standard groups ---
hi def link jstarKeyword      Statement
hi def link jstarConditional  Conditional
hi def link jstarRepeat       Repeat
hi def link jstarException    Exception
hi def link jstarInclude      Include
hi def link jstarFunction     Define
hi def link jstarClass        Define
hi def link jstarFunctionCall Function
hi def link jstarDecorator    Define
hi def link jstarClassName    Type
hi def link jstarConstant     Constant
hi def link jstarComment      Comment
hi def link jstarShabang      Comment
hi def link jstarTodo         Todo
hi def link jstarString       String
hi def link jstarNumber       Number
hi def link jstarOperator     Operator
hi def link jstarDelimiter    Delimiter

let b:current_syntax = "jstar"

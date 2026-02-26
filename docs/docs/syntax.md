---
layout: default
title: Syntax
nav_order: 1
description: "J* syntax"
parent: Documentation
permalink: docs/syntax
---

# Syntax
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

**J\*** has a syntax pretty similar to Lua, while borrowing some elements from Python and C.  
The main ways in which the syntax diverges from the one of Lua are the absence of the `do` and 
`then` keywords to start control statement bodies, the significance of newlines in the source code 
and the addition of some keywords.

## Comments

In **J\*** comments starts after a `//` and run until the end of a line, just like in c:
```jstar
// This is a comment
// Another comment
```

## Newlines

As mentioned above, newlines are significant in **J\*** code. They are mainly used to separate 
statements:
```jstar
print("Statement 1") // \n
print("Statement 2")
```

Doing something like this will instead result in a parsing error:
<pre class="runnable-snippet">
print("Statement 1") print("Statement 2")
</pre>

If you want to put two or more statements on a single line, you can separate them using a semicolon:
<pre class="runnable-snippet">
print("Statement 1"); print("Statement 2")
</pre>

Semicolons followed by a newline are also accepted, making code like this possible, even though not
reccomended:
```jstar
print("Statement 1");
print("Statement 2");
```

Even though most of the newines are significant, in some circumntances the parser can ignore them.
In fact, every time a newline is encountered, if the last token cannot end a statement or an 
expression, then the newline is skipped. For example:
```jstar
print(4 + // `+` cannot end the expression, the newline is ignored
        2)
```

If you instead want to ignore a newline no matter what, you can escape it using the `\` token:
```jstar
// `4` is a valid end for an expression, but the newline is ignored anyway
print(4 \
    + 2)
```

## Keywords
This is a list of all the keywords of the language:
```jstar
    true false null and or else for fun native if elif
    var while in begin end as is try ensure except raise 
    with continue break static end return import super
    class
```
These names are reserved, and cannot be used as names for variables, classes or functions.

## Identifiers

In **J\*** *identifiers* (also called *names*) start with  a letter or an underscore, and can be 
followed by any number of letters, underscores and digits.  
In other words, any name that matches this regex is a valid identifier in **J\***, as long as it's 
not also one of the reserved keywords:
```bash
[_a-zA-Z][_a-zA-Z0-9]*
```

## Operators precedence

**J\*** provides all the usual operators supported by other programming languages and some more
exotic ones. We will discuss the semantics of all operators and the way they behave on different 
types in future sections. For now we limit ourselves to list them, from highest to lowest precedece:

| Operator                                   | Description                                    | Associativity  |
|:-------------------------------------------|:-----------------------------------------------|:---------------|
| `(expr)` `[expr, ...]` `{key: value, ...}` | Grouping, List literal, Table literal          | Left           |
| `x[]` `x()` `x.attr`                       | Subscript, Call, Attribute access              | Left           |
| `^`                                        | Exponentiation                                 | Right          |
| `-x` `!x` `~x` `#x` `##x`                  | Negation, Not, Complement, Length, Stringify   | Right          |
| `*` `/` `%`                                | Multiplication, Division, Remainder            | Left           |
| `+` `-`                                    | Addition, Subtraction                          | Left           |
| `<<` `>>`                                  | Left and right shift                           | Left           |
| `&`                                        | Bitwise and                                    | Left           |
| `~`                                        | Bitwise xor                                    | Left           |
| `|`                                        | Bitwise or                                     | Left           |
| `<` `<=` `>` `>=` `==` `!=` `is`           | Relational                                     | Left           |
| `and`                                      | Boolean and                                    | Left           |
| `or`                                       | Boolean or                                     | Left           |
| `fun` `|| => expr`                         | Function literal, Lambda                       | Right          |
| `if-else`                                  | Ternary                                        | Right          |
| `,`                                        | Comma operator (Tuple literal)                 | Left           |
| `=` `+=` `-=` `*=` `/=` `%=`               | Assigment                                      | Right          |

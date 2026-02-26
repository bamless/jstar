---
layout: default
title: Control flow
nav_order: 7
description: "Control flow"
parent: Documentation
permalink: docs/control-flow
---

# Control flow
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Control flow structures are statements used to selectively execute code based on certain conditions.
They usually come in two flavours: *branching* and *looping* control flow structures.

## Truthiness and falsyness

As already mentioned before, all values in the **J\*** language have an intrisic truth value
associated with them. This is useful for resolving situations in which types other than booleans are
used as coditions for control flow statements. In some languages - especially statically typed 
ones - this situation cannot arise, as the compiler will check the type of any condition and fail if 
it doesn't find a boolean. For a dynamic language like  **J\*** though, it feels unnatural to fail 
with a runtime error everytime such a situation presents itself. So, we follow the approach taken by
other scripting language - and notably some statically typed ones, like c or c++ - and partition the
set of all values in two: those which evaluate to true, and those which evaluate to false. We call
them truthy and falsy values, respectively.

The choice of wich values should be truthy and wich falsy is somewhat arbitrary and all languages
have their own rules. **J\*** follows an approcach similar to the one of Lua: `null` and `false` are
falsy, any other value is truthy. This means that values such as `0`, the empty string `""` and
empty sequences `[]` or `()` all evaluate to true, unlike some other languages.

## If statement

The if statement is an example of a *branching* control flow structure. It is used to selectively
execute chunks of code based on a condition:
<pre class='runnable-snippet'>
if 2 < 4
    print("Two is less than four")
end
</pre>

If statements provide an optional `else` clause that will be executed when the condition is false:
<pre class='runnable-snippet'>
var found = false

if found
    print("Found it!")
else
    print("Not found, sorry")
end
</pre>

If you have a series of conditions that you want to check one after the other, you can use
`elif` clauses:
<pre class='runnable-snippet'>
var num = 10

if num < 3
    print("num is less than three")
elif num < 10
    print("num is less that ten")
elif num == 10
    print("num is equal to ten")
else
    print("num is greater that ten")
end
</pre>

## While statement

The while statement enables you to repeat a chunk of code as long as a condition holds true:
<pre class='runnable-snippet'>
// Exponentiation by squaring: x^n
var x = 2
var n = 6

var y = 1
while n > 1
    if n % 2 == 0
        x = x * x
        n = n / 2
    else
        y = x * y
        x = x * x
        n = int((n - 1) / 2)
    end
end

print(x * y)
</pre>

At every iteration, before executing its body, the while statement evaluates its expression. If
it evaluates to true then the body is executed, otherwise the iteration is stopped and statements
following the while are executed.

While is an example of a *looping* control flow structure.

## For statement

A for statement is composed by four parts: an *initializer*, a *condition*, an *action* and a 
*body*. It looks like this:
<pre class='runnable-snippet'>
for var i = 0; i < 10; i += 1
    print("counting {0}" % i)
end
</pre>

If you come from c or javascript then this syntax will be familiar to you. Its semantic are the
following:  
 1. When first entering the loop, the *initializer* is executed. This can be either an expression
    or a statement. In the example above is a *variable declaration* statement that introduces a new
    variable *i* in the scope of the loop.
 2. The *condition* is evaluated. This behaves like the condition of a *while*. If it evaluates to
    `false` then the loop is stopped, otherwise proceed to step `3`.
 3. The body is executed.
 4. The *action* expression is executed.
 5. Repeat this process from step `2`.

This seems a lot of work to just increment a variable to ten. Indeed, this kind of loop is a bit
verbose, but what it loses to its verbosity it gains in flexibility. You can do pretty much all you
want to by using a for statement.

Counting backwards? Just replace the head of the for loop above with:
```jstar
for var i = 10; i >= 0; i -= 1
```

Count in multiples of two? easy:
```jstar
for var i = 2; i < 256; i *= 2
```

Iterating a linked list? easy as well:
```jstar
for var l = linkedList; l != null; l = l.next
```

All of the components of a for loop aside from its *body* are optional. If the condition is omitted
it is assumed to aways evaluate to `true`. This can be used to create infinite loops:
```jstar
for ;;
    print("Infinite")
end
```

By instead omitting all components other than the condition we can emulate a while loop:
```jstar
for ; condition;
    print("Executing body...")
end
```
Even though it is not as elegant.

## Foreach statement

Often the only thing we want to do is to iterate over the elements of a collection. To achieve this
task **J\*** provides a for-each loop:
<pre class='runnable-snippet'>
var list = [1, "foo", false, "bar"]

for var element in list
    print(element)
end
</pre>

Pretty elegant isn't it?

A for-each will walk over all the elements of an "iterable" object, binding the elements to the 
provided variable, until it is exausted.

Two built-in values that are considered "iterable" are lists and tuples, but you can also create
your own custom iterable objects. More info about this in the [iterator protocol](iterator-protocol)
section.

## Break statement

Sometimes we want to be able to stop a loop even though we are in the middle of executing its body.
To do this, **J\*** provides a break statement:
<pre class='runnable-snippet'>
for var i = 0; i < 10; i += 1
    print(i)
    if i == 5
        break
    end
end
</pre>

Break can be used with all the kinds of loops discussed above.

## Continue statement

Instead of stopping a loop prematurely, sometimes we want to be able to skip the current iteration
and proceed to the next, usually based on some condition. This can be achieved with a continue
statement:
<pre class='runnable-snippet'>
for var i = 1; i <= 10; i += 1
    if i % 2 != 0 // The number is odd, skip to the next
        continue
    end
    print("{0} is even" % i)
end
</pre>

---
layout: default
title: Variables
nav_order: 6
description: "Variables"
parent: Documentation
permalink: docs/variables
---

# Variables
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Variables are named locations used to store values. In **J\*** the `var` keyword is used to
introduce a new variable in the current *scope*. We call this a variable *declaration* or 
*definition*:
<pre class='runnable-snippet'>
var newVariable = "A variable"
print(newVariable)
</pre>

In **J\*** the concept of an *uninitialized* variable doesn't exist. If a variable omits an
initializer, it is implicitly initialized with `null`:
<pre class='runnable-snippet'>
var newVariable
print(newVariable)
</pre>


As mentioned in the [syntax](syntax) section, in **J\*** variables do not have the concept of
*type*, so it is legal to assign values of different types to them:
<pre class='runnable-snippet'>
var newVariable = "A variable"
print(newVariable)

newVariable = 5
print(newVariable)
</pre>

In the example above we see the first instance of an *assignment* expression. In fact, once created
a variable can be reassigned at will, as in **J\*** we do not have a concept of constant variables.
Instead, when we want to indicate that a variable is constant and should not be modified, we give it
an all CAPS name:
<pre class='runnable-snippet'>
var PI = 3.141592
print(PI)
</pre>

This is just a convension though, and the compiler will not enforce it.

## Scope

**J\*** implements true lexical block scope. This means that, once a value is declared, it will
exist until the current block ends. 

A new block can be introduced with the `begin` and `end` keywords:
<pre class='runnable-snippet'>
var a = 1
begin // << new block starts
    var a = "one"
    print(a)
end   // << block ends, the `a` of this block is discarded
print(a)
</pre>

The example above also showcases variable *shadowing*. A variable in a inner block can have the same
name of a variable in an outer one. This feature is useful in some situations but shouldn't be
overused, as it can create confusion.

Also, a new block scope is introduced by all [control flow](control-flow)
structures:
<pre class='runnable-snippet'>
var condition = true

if condition        // << `if` block start
    var a = "true"  // << new variable in current scope
    print(a)
else                // << `if` block ends `a` is discarded, `else` block starts
    var a = "false" // << new variable in current scope
    print(a)
end                 // << `else` block ends, `a` is discarded
</pre>

And [functions](functions):
```jstar
fun someFunction() // << function scope starts
    var a = 1
end                // << function scope ends, `a` is discarded
```

## Module variables

All variables declared in the top-level scope are called *module variables*. In other words they are
variables that are visible to the [module system](modules). These behave differently from variables
declared in inner scopes, also called *local variables*. In fact, *module variables* in **J\***
are *late bound*. This means that you can refer to variables that haven't been declared yet, and,
as long as that code doesn't run prior to the variable being defined, it will resolve it
succesfully.

<pre class='runnable-snippet'>
// Late bound module variables are handy when working with functions

// Here, we define a function that accesses `moduleVar`
// Note that the functions doesn't get executed yet, just defined
fun getVariable()
    return moduleVar
end

// Define the variable before the execution of `getVariable`
var moduleVar = "module variable"

// Everything works as expected, `moduleVar` is correctly resolved
print(getVariable())
</pre>

If instead we execute some code that refers to a *module variable* prior to its definition, we
get a runtime error:
<pre class='runnable-snippet'>
// Differently from the function body of the example 
// above, this code is executed immidiately
print(moduleVar)

// This definition isn't even reached, as we fail with an error above
var moduleVar = "module variable"
</pre>

Another peculiarity of *module variables* is that they can be declared multiple times:
<pre class='runnable-snippet'>
var a = 1
print(a)

var a = "one"
print(a)
</pre>

This will not be true for *local variables*

## Local variables

All other variables defined in a scope that it's not the top-level one are called *local variables*.
Local variables are not *late bound* and are resolved at compile time. Also, they aren't visible to
the [module system](modules).

Trying to declare a *local variable* more than once in the same scope will result in a compile 
error:
<pre class='runnable-snippet'>
begin
    var a = 1
    var a = "one"
end
</pre>

Also, it is illegal to use a newly declared local variable in its initializer:
<pre class='runnable-snippet'>
begin
    var a = 5 + a
end
</pre>

The problem in the example above is that `a` **does** provide an initializer, so it isn't
implicitly initialized to `null`. But this means that during the execution of the initializer
itself `a` will be uninitialized. This cannot happen in **J\***, and thus we fail with an error if a
situation like this presents itself.

## Assignments

As already mentioned above, in **J\*** variables can be assigned to by using this syntax:
<pre class='runnable-snippet'>
var a = "variable"
a = "reassign"
print(a)
</pre>

An assignment expression is formed by two parts: an expression appearing to the left of the `=`
sign, the *lvalue*, and an expression to the right, the *rvalue*. Not all expressions can be used
as *lvalues*. Valid ones include:
 - A bare identifier, called a *variable literal*: `name = ...`
 - An attribute access expression: `x.attr = ...`
 - A subscript expression: `sequence[0] = ...`
 - A tuple composed of expressions satisfying the rules above: `a, x.b, c[0] = ...`

That last one is an [unpacking assignment](variables#unpacking-assignment).

Trying to use any other expression as an *lvalue* will result in a parsing error:
<pre class='runnable-snippet'>
5 = 3
</pre>

The assignment itself is an expression in **J\***, and when used in larger expressions it evaluates
to the assigned value:
<pre class='runnable-snippet'>
var a
print(5 + (a = 5))
print(a)
</pre>

The `=` operator is not the only one that can be used in an assignment. A handful of so called
*compound* assigment operators are provided:
```jstar
a += 1 // expands to `a = a + 1`
a -= 1 // expands to `a = a - 1`
a *= 2 // expands to `a = a * 2`
a /= 4 // expands to `a = a / 4`
a %= 3 // expands to `a = a % 3`
```

They are useful when the operand in the *rvalue* expression is the variable itself.

## Unpacking

Sometimes, when we have a list or a tuple we would like to extract some of its values to variables. 
We can do this with the subscript operator, but its pretty verbose:
```jstar
var point = 20, 10, 8
var x = point[0]
var y = point[1]
var z = point[2]
```

To perform this common operation, **J\*** provides a special syntax for both variable declarations
and assignments, collectively called *unpacking*.

### Unpacking variable declaration

An *unpacking variable declaration* looks like this:
<pre class='runnable-snippet'>
var point = 20, 10, 8
var x, y, z = point // Unpacking variable declaration
print(x, y, z)
</pre>

The `var` keyword is followed by multiple names separated by a comma. If an initializer is provided,
it must be a tuple or a list, and its values will be extracted into the newly declared variables,
from first to last. If instead an initializer is not provided, all the variables will be initialized
to `null` as usual.

If the list or tuple doesn't have enough elements to initialize all of the variables, an error will 
be produced:
<pre class='runnable-snippet'>
var a, b, c = 1, 2
</pre>

If it does have more values than the variables, then the extra ones will be ignored:
<pre class='runnable-snippet'>
var a, b, c = 1, 2, 3, 4, 5
print(a, b, c)
</pre>

Unpacking a single element works by appending a trailing comma to the declaration name:
<pre class='runnable-snippet'>
var single, = 1, 2, 3
print(single)
</pre>

### Unpacking assignment

An *unpacking assignment* looks similar to an *unpacking variable declaration*:
<pre class='runnable-snippet'>
var point = 20, 10, 8
var x, y, z     // Declare variables first

x, y, z = point // Unpacking assignment
print(x, y, z)
</pre>

In an unpacking assignment, the *lvalue* of the assignment is a tuple composed of only *lvalues*,
and the *rvalue* must be a tuple or a list. If these conditions are not met, then errors will be
produced:
<pre class='runnable-snippet'>
var a, b, c
a, b, c = "not a tuple"
</pre>

<pre class='runnable-snippet'>
1, 2, 3 = 4, 5, 6
</pre>

Similarly to unpacking variable delarations, not having enough values in the *rvalue* will produce
an error, while having more than needed will ignore the extra ones:
```jstar
a, b, c = 1, 2, 3, 4, 5 // extra values will be ignored
a, b, c = 1, 2          // error, not enough values
```

To unpack a single element, simply use a single element tuple as the *lvalue*:
<pre class='runnable-snippet'>
var single

single, = 1, 2, 3
print(single)
</pre>

## Unpacking tricks

Unpacking variable declarations and assignments can be used to solve common problems in an elegant
way.

For example returning multiple values from a [function](functions):
<pre class='runnable-snippet'>
fun multipleReturn()
    return 1, 2, 3 // return a tuple
end

// Unpack the returned tuple
var a, b, c = multipleReturn()
print(a, b, c)
</pre>

Or swapping two elements of a sequence without using temporary variables:
<pre class='runnable-snippet'>
var list = [1, 2, 3, 4, 5]

list[0], list[4] = list[4], list[0]
print(list)
</pre>

The usage of unpacking makes the code more readable and maintainable, and you should use it when
given the occasion.
---
layout: default
title: Functions
nav_order: 8
description: "Functions"
parent: Documentation
permalink: docs/functions
---

# Functions
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Functions are a bundle of statements and expressions that are combined to perform a task, and that 
can be used repeatedly. They can accept input data to be operated on in the form of parameters, and
return some data to the caller via *return* statements.

In **J\*** functions are an implementation of *closures* and as such they support the concept of
[upvalues](functions#upvalues). Also, just like everything else in **J\***, they are first-class
values. This means that they can be stored in variables, passed in as arguments to other functions
and can even be returned as a result of a function call.

## Function definitions
The main way that **J\*** lets you create functions is through a *function definition* statement:
```jstar
fun printFoo()
    var foo = "Foo"
    print(foo)
end
```

In **J\*** a function definition is an executable statement. Its execution creates a new *function
object* (an object containing the executable code of the body) and binds it to the provided name in
the current scope:
<pre class='runnable-snippet'>
fun newFunction()
    print("This is a function!")
end

print(newFunction) // the function is bound to `newFunction` in the current scope!
</pre>
Note that the function definition does not execute the function body; this gets executed only when
the function is [called](functions#calling-functions).

Being a statement, a function definition can appear in every place a statement can, even inside 
other functions:
```jstar
fun function()
    // create a new function and bind its name to a variable 
    // named `nested` inside `function` scope
    fun nested()
        print("A nested function")
    end
    print("Outer function")
end
```

## Function literals

**J\*** provides another way of creating functions: *function literals*:
<pre class='runnable-snippet'>
var newFunction = fun()
    print("This is a function!")
end

print(newFunction)
</pre>

Function literals, differently from function definitions, are expressions, and as such can be used
in every context where an expression can, such as in a variable initializer as showed in the example
above.

They are often used to create so called "anonymous" functions: one-time use functions
that are not meant to be bound to a name, particularly useful for callbacks:
<pre class='runnable-snippet'>
fun callMe(fn)
    fn()
end

callMe(fun()
    print("I've been called!")
end)
</pre>

Function literals can also be used in conjuction with variables to achieve the same effect of a 
*function definition* statement:
<pre class='runnable-snippet'>
var newFunction
newFunction = fun()
    print("New function")
end
</pre>

In fact, when **J\*** sees a *function definition* statement, it desugars it (in other words, 
trasforms it) into the form above, making them equivalent. Nontheless, if the only thing you need to
do is create a function and bind it to a name, you should prefer using a *function definition*, as
it is more natural to write and less verbose.

## Lambdas

It is not uncommon for function literals to be composed of a single return statement. Being such a
common use case, **J\*** provides a special syntax for creating such functions:
```jstar
var lambda = |x, y| => x + y // A lambda body is composed of a single expression
```

This is called a *lambda* function literal, and the compiler will desugar it into the following
function literal:
```jstar
var lambda = fun(x, y)
    return x + y
end
```

## Calling functions

As already mentioned, creating a function via function definitions or literals doesn't execute its
body. When you want to execute a function you must *call* it. This is done by using the *call
operator* `()`:
<pre class='runnable-snippet'>
fun someFunction()
    print("'someFunction' called")
end

someFunction() // Function call
</pre>

A function call is an *expression*, and as such can be used in every place an expression can.

## Function parameters

It would be pretty limiting if functions could only operate on the same data every time they're
called.  Fortunately, functions can specify input parameters that can be varied between function
calls:
<pre class='runnable-snippet'>
fun foo(a, b)
    print("Called foo with {0} and {1}" % (a, b))
end

foo("bar", 49)
foo(false, null)
</pre>

Function parameters in **J\*** are *positional*. This means that the arguments passed to the call
operator will be bound to the parameters based on their order.

Also, the number of arguments between the function definition and the function call must match,
passing too many or too few will result in an error:
<pre class='runnable-snippet'>
fun foo(a, b)
    print("Called foo with {0} and {1}" % (a, b))
end

foo("bar", 49, 50)
</pre>

## Default parameters

Sometimes, it can be useful to have default values for function parameters, so that some of them can
be left unspecified at the call site. This can be achieved using this syntax:
<pre class='runnable-snippet'>
fun foo(a, b="Default value")
    print("Calling with {0} and {1}" % (a, b))
end

foo("bar")     // If left unspecified, `b` will be bound to "Default value"
foo("bar", 49) // If instead a second argument is passed, it will take the place of the default one
</pre>

Default parameters *must* appear after all positional ones have been listed (if any). Specifying a
positional parameter after a default one will result in an error:
<pre class='runnable-snippet'>
fun foo(a="Default", b)
    print("Calling with {0} and {1}" % (a, b))
end
</pre>

Valid values for default paramters are: *strings*, *numbers*, *booleans* and *null*: the
*constant values*. If you try to use any other **J\*** value the compiler will scream at you:
<pre class='runnable-snippet'>
fun foo(a, b=[1, 2, 3])
    print("Calling with {0} and {1}" % (a, b))
end
</pre>

## Variadic functions

For some functions it can be useful to accept an unlimited number of arguments. In **J\*** we call
such functions *variadic functions*. A function is variadic if the last parameter is an *ellipsis*
(`...` token):
```jstar
fun variadic(a, b, ...)
    // Function body
end

// We can pass extra arguments in addition to `a` and `b`
variadic(1, 2, 3, 4, 5)
```

When calling a variadic function, any extra argument will be put in a tuple and passed to the
function via an hidden parameter called *args*:
<pre class='runnable-snippet'>
fun variadic(a, b, ...)
    print("Argument 1:", a)
    print("Argument 2:", b)
    for var e in args // `args` holds the extra arguments
        print("Variadic:", e)
    end
end

variadic(1, 2, 3, 4, 5)
</pre>

If instead no extra parameters are passed at the call site, `args` will be bound to the empty tuple:
<pre class='runnable-snippet'>
fun variadic(...)
    print(args)
end

variadic()
</pre>

Variadic functions can still use positional and default parameters, with the usual rules: positional
first, then default ones:
<pre class='runnable-snippet'>
// Using all kinds of parameters in a function
fun all(a, b, c="Default", ...)
    print(a, b, c, args)
end

all(1, 2)                  // Calling by specifying only positional parameters
all(1, 2, "foo")           // Calling by specifying positional and default parameters
all(1, 2, "foo", false, 3) // Calling with extra parameters after positional and defaults
</pre>

## Unpacking call
Just like with variables, sometimes we wish to extract the elements of a tuple or a list and pass
them as arguments to a function call. This can be achieved via an *unpacking* function call:
<pre class='runnable-snippet'>
fun foo(a, b, c)
    print(a, b, c)
end

var unpackable = 1, 2, 3
foo(unpackable)... // Unpacking call
</pre>

An *unpacking* call is composed by a normal function call followed by an *ellipsis*. When an 
unpacking call is executed, **J\*** will try to unpack the last argument and bind its values to the
remaining parameters.

If the last argument of the call is not a list or a tuple, an error will be produced:
<pre class='runnable-snippet'>
fun foo(a, b, c)
    print(a, b, c)
end

foo("not a tuple")...
</pre>

Also, if the number of elements in the list or tuple doesn't match the number of parameters that
haven't yet been specified, an error will be produced as well:
<pre class='runnable-snippet'>
fun foo(a, b, c)
    print(a, b, c)
end

// The first parameter is specified, this leaves only 2 that can be 
// bound by the unpacking call.
// But the provided tuple has 3 elements, so the call will fail.
// The same would happen even if the tuple has a low item count, such as 1
foo(1, (2, 3, 4))...
</pre>

## Keyword parameters

As already enstablished, functions in **J\*** only support positional parameters. Nontheless, it
would be useful for a function to accept named parameters (i.e. keyword parameters), especially when
a function has lots of them and remembering their position by heart is difficult. **J\*** supports
this method of parameter passing by using tables in conjunction with function calls:
<pre class='runnable-snippet'>
fun keywordParams(kwargs)
    if kwargs["foo"]
        print("Parameter foo is {0}" % kwargs["foo"])
    else
        print("No foo parameter specified")
    end
end

keywordParams{"foo" : 49}
</pre>

Note how the function call above uses curly braces instead of normal ones used in regular function
calls. This is actually syntactic sugar that gets expanded by the compiler into this:
```jstar
keywordParams({"foo" : 49})
```
i.e. a function call with a single argument that is a table.

The notation using curly braces already makes calls with keyword arguments pretty natural, but using
strings in the middle of a function call to name its parameters is a bit ugly. Fortunately, we can
use an alternate syntax for table literals to alleviate this problem:
<pre class='runnable-snippet'>
fun keywordParams(kwargs)
    if kwargs["foo"]
        print("Parameter foo is {0}" % kwargs["foo"])
    else
        print("No foo parameter specified")
    end
end

keywordParams{.foo : 49}
</pre>

When a key of a table literal is composed by a dot followed by an identifier, then that identifier
is treated as a string. This makes the example above equivalent to the first one, but much more
pleasant to write and to read. As an additional plus, by using this syntax the named parameters are
forced to be valid **J\*** identifiers, making them consistent with positional ones.

## Returning results

Results can be returned by functions via *return statements*:
<pre class='runnable-snippet'>
fun add(a, b)
    return a + b
end

print(add(5, 2))
</pre>

A *return statement* is composed by a `return` keyword followed by an optional expression. When a
function reaches a return statement, the execution of the function is stopped and control is 
*returned* to its caller, that will receive the argument of the return as a result.

Just like variables and parameters, functions do not need to specify a type for their return value,
and as such it is legal to return different types in different execution paths of the same
function:
<pre class='runnable-snippet'>
fun fiveOrErr(n)
    if n == 5
        return n
    else
        return "`n` must be a five!"
    end
end

print(fiveOrErr(5))
print(fiveOrErr(20))
</pre>

Also, as mentioned in the beginning of the paragraph, the expression part of a return statement is
optional. If a bare return is encountered, then its argument will be assumed to be `null`.

Another property to note about functions and return values is that, in **J\***, all functions do
return one. In fact, if no return statement is encountered during the execution of a function, its
return value will be `null`:
<pre class='runnable-snippet'>
fun noReturn()
end
print(noReturn())
</pre>

This enables the writing of so called 'void' functions (functions that do not return a result),
without having to type in a return statement at the end of every function.


## Closures and upvalues

We have already seen how functions can be nested inside one another. This can create some intresting
situations:
```jstar
fun createCounter()
    var counter = 0
    return fun()
        counter += 1
        return counter
    end
end
```

Here, we can see how the nested function literal accesses the *counter* variable, that is declared
in its surrounding function. Given **J\*** block scope rules, it is natural to assume that the
`counter` used inside the inner function will actually referer to the counter variable declared in
its parent. The strange thing is that, when we actually call `createCounter`, we return a function
that references a variable of `createCounter` itself, that in the meantime completed execution and
thus, given the already discussed scoping rules, has a reference to a variable that is no longer in
scope. How does this work? Aren't variables supposed to be discared when their scope ends? The
general answer to this is yes but, in the case of functions, the situation is a bit more
involved.

Functions in **J\*** are actually an implementation of *closures*. A *closure* is a
function that *closes over* or, in other words, *captures* free variables in surrounding scopes.
Their working is not dissimilar to that of nested scopes, but applied to functions: if a variable
is referenced that is not declared in the current function, search for it in any of its parents.
Differently from scopes though, functions in **J\*** are first-class values. This means that a
function can actually escape its scope, for example by being returned as a result. This can create
a situation in which the lifetime of the closure is longer than the lifetime of the variables that
it references. For this reason, closures mantain references to *captured* variables so that they
remain alive for as long as the closure that captured them is alive. We call such variables 
*upvalues*.

As a consequence of this mechanism, closures can mantain a state in the form of upvalues, and as
such can be used to implement structures similar to objects as showed in the `createCounter`
example:
<pre class='runnable-snippet'>
fun createCounter()
    var counter = 0
    return fun()
        counter += 1
        return counter
    end
end

var count = createCounter()
print(count())
print(count())
print(count())
</pre>

In fact, we could go as far as to say that [closures are a poor man's objects](http://wiki.c2.com/?ClosuresAndObjectsAreEquivalent).
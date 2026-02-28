---
layout: default
title: Iterator protocol
nav_order: 9
description: "Iterator protocol"
parent: Documentation
permalink: docs/iterator-protocol
---

# Iterator protocol
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

An *iterable* is any object that can be traversed element by element. Iterables are used by the
[foreach statement](control-flow#foreach-statement), by [spread expressions](functions#unpacking-call)
(`...`), and by a rich set of built-in adapters and collectors that compose together without
allocating intermediate collections.

## The protocol

To make a class iterable, implement two methods:

| Method | Signature | Role |
|:---|:---|:---|
| `__iter__` | `__iter__(state)` | Advance to the next element; return the new state, or `false` to stop. |
| `__next__` | `__next__(state)` | Return the element for the state produced by the last `__iter__` call. |

The `state` parameter carries whatever information the iterator needs to know its position —
an index for sequences, a node pointer for linked structures, or anything else. On the very first
call `state` is always `null`, which signals that iteration is just beginning.

Concretely, `for var e in obj` desugars to the following loop:
```jstar
var state = null
for ;;
    state = obj.__iter__(state)
    if !state
        break
    end
    var e = obj.__next__(state)
    // loop body
end
```

`__iter__` may return any truthy value to continue, or `false` to stop. `__next__` is only called
when `__iter__` returns a truthy state, so it can safely assume that the state is valid.

## Implementing a custom iterable

Any class that implements `__iter__` and `__next__` becomes iterable. Note that, even though it's not required,
inheriting from `iter.Iterable` is a common practice when implementing custom classes that can be
iterated. Inheriting from this class makes a lot of useful adapter methods available to the custom
class, such as `take`, `map`, `filter`, etc.

> NOTE: `take`, `map`, `filter` and other similar methods are also provided as plain functions in the
> `iter` module. Using them like this is not as convienient as the wrapper methods provided by
> `iter.Iterable` though, especially because method calls can be conviniently chained:
> `container.take(10).map(|e| => e * 2).filter(|e| => e > 10)`

Here is a simple countdown sequence as an example:
<pre class='runnable-snippet'>
class Countdown is iter.Iterable
    construct(from)
        this._from = from
    end

    // On the first call state is null, so we start at this._from.
    // On subsequent calls we decrement, stopping when we pass zero.
    fun __iter__(state)
        if state == null
            return this._from if this._from >= 0 else false
        end
        return state - 1 if state > 0 else false
    end

    fun __next__(state)
        return state
    end
end

for var n in Countdown(5)
    print(n)
end
</pre>

Because `Countdown` inherits from `Iterable`, it automatically gains the full set of
[adapter and collector methods](#adapters) described later in this page:
<pre class='runnable-snippet'>
class Countdown is iter.Iterable
    construct(from)
        this._from = from
    end

    fun __iter__(state)
        if state == null
            return this._from if this._from >= 0 else false
        end
        return state - 1 if state > 0 else false
    end

    fun __next__(state)
        return state
    end
end

print(Countdown(10).filter(|n| => n % 2 == 0).take(3).sum())
</pre>

## The `Sequence` class

For collections that also support random access by index, inherit from `Sequence` instead.
`Sequence` extends `Iterable` and requires three additional methods:

| Method | Description |
|:---|:---|
| `__get__(idx)` | Return the element at index `idx`. |
| `__set__(idx, val)` | Set the element at index `idx` to `val`. |
| `__len__()` | Return the number of elements. |

In exchange, `Sequence` provides efficient implementations of `count`, `nth`, `first`, `last`,
as well as extra methods like `contains`, `indexOf`, `indexOfLast`, `reversed` and `empty` that
are only meaningful for random-access collections.

Built-in types `List`, `Tuple` and `String` are all `Sequence`s.

## Generators

The simplest way to create a custom iterable is a *generator function* — a function that uses
the `yield` keyword. Calling a generator function does not execute its body immediately; it
returns a `Generator` object. Each time the generator is advanced (by a foreach loop or an
adapter), execution resumes from where it last suspended until the next `yield`, which produces
one element. When the function returns normally the generator is exhausted.
<pre class='runnable-snippet'>
fun naturals()
    var n = 0
    for ;;
        yield n
        n += 1
    end
end

for var n in naturals().take(5)
    print(n)
end
</pre>

Because `Generator` implements the iterator protocol, generators compose freely with all the
adapters and collectors described below:
<pre class='runnable-snippet'>
fun naturals()
    var n = 0
    for ;;
        yield n
        n += 1
    end
end

var result = naturals().
    filter(|n| => n % 2 != 0). // odd numbers
    map(|n| => n * n).         // square them
    take(5).                   // first five
    sum()                      // add them up

print(result) // 1 + 9 + 25 + 49 + 81
</pre>

### Generator state

A generator can be in one of four states:

| State | Meaning |
|:---|:---|
| Started | The generator was just created and has not yet run. |
| Running | The generator is currently executing (i.e. we are inside its body). |
| Suspended | The generator has yielded a value and is waiting to be resumed. |
| Done | The generator function returned; no more values will be produced. |

The `isDone()` method returns `true` when the generator is in the *Done* state.

### Advanced generator control

Beyond plain iteration, generators support three additional operations:

**`send(val)`** — resume the generator and make the `yield` expression evaluate to `val` inside
the generator body. This is how values can be passed *into* a running generator:
<pre class='runnable-snippet'>
fun accumulator()
    var total = 0
    for ;;
        var n = yield total
        if n == null
            break
        end
        total += n
    end
end

var gen = accumulator()
gen.send(null) // start the generator (first yield)
gen.send(10)
gen.send(20)
print(gen.send(5))  // 35
</pre>

**`throw(exc)`** — resume the generator by raising the given exception at the point where it is
suspended. If the generator does not catch it, the exception propagates to the caller.

**`close()`** — signal the generator to stop. If the generator is suspended inside a `with`
block or `ensure` clause, those are still executed before the generator transitions to the *Done*
state.

## Built-in sources

The `iter` module provides several ready-made generator functions that produce iterables:

| Source | Description |
|:---|:---|
| `range(stop)` | Integers `0, 1, …, stop - 1`. |
| `range(start, stop, step=1)` | Integers from `start` to `stop` (exclusive) by `step`. |
| `repeat(val)` | Infinite repetition of `val`. |
| `repeatWith(fn, ...)` | Infinite repetition of `fn(...)`. |
| `once(val)` | A single-element iterable containing `val`. |
| `onceWith(fn, ...)` | A single-element iterable containing `fn(...)`. |
| `fromFun(fn)` | Calls `fn()` repeatedly, stopping when it returns `null`. |
| `successors(init, fn)` | Yields `init`, then `fn(init)`, `fn(fn(init))`, … until `null`. |
| `empty` | An iterable that yields no elements. |

<pre class='runnable-snippet'>
for var i in iter.range(2, 11, 2)
    print(i) // 2, 4, 6, 8, 10
end
</pre>

<pre class='runnable-snippet'>
print(iter.successors(1, |n| => n * 2 if n < 64 else null).sum())
</pre>

## Adapters

Adapters are lazy: they wrap an iterable and produce a new iterable without consuming the source
or allocating an intermediate collection. They are available both as free functions and as methods
on any `Iterable`:

| Adapter | Description |
|:---|:---|
| `map(fn)` | Apply `fn` to each element. |
| `filter(predicate)` | Keep only elements for which `predicate` returns truthy. |
| `take(n)` | Yield at most `n` elements. |
| `takeWhile(predicate, inclusive=false)` | Yield elements while `predicate` holds; optionally include the first failing element. |
| `skip(n)` | Skip the first `n` elements. |
| `skipWhile(predicate)` | Skip elements while `predicate` holds, then yield the rest. |
| `enumerate(start=0)` | Yield `(index, element)` tuples starting at `start`. |
| `zip(other)` | Yield `(a, b)` tuples pairing elements from two iterables; stops at the shorter one. |
| `concat(other)` | Yield all elements of this iterable followed by all elements of `other`. |
| `flatten()` | Recursively yield elements of nested iterables. |
| `flatMap(fn)` | Apply `fn` to each element and flatten the results one level. |
| `interleave(sep)` | Insert `sep` between every pair of consecutive elements. |
| `chunks(n)` | Yield successive non-overlapping tuples of `n` elements. |
| `repeat(n=null)` | Repeat the iterable `n` times, or infinitely if `n` is `null`. |
| `sorted(comparator=null)` | Collect all elements into a sorted list (not lazy). |
| `apply(fn, ...)` | Pass this iterable as the first argument to `fn`, forwarding any extra arguments. |

<pre class='runnable-snippet'>
// Pairs of (index, square) for the first 5 even numbers
for var pair in iter.range(20).filter(|n| => n % 2 == 0).take(5).enumerate()
    print(pair)
end
</pre>

<pre class='runnable-snippet'>
// Word histogram using chunks and zip
var words = ["a", "b", "c", "d", "e", "f"]
for var chunk in words.chunks(2)
    print(chunk)
end
</pre>

## Terminal collectors

Terminal collectors consume the iterable and produce a single result. Like adapters, they are
available as both free functions and methods on `Iterable`:

| Collector | Description |
|:---|:---|
| `sum(start=0)` | Sum all elements, starting from `start`. |
| `count()` | Count the number of elements. |
| `any(predicate=null)` | Return `true` if any element (or any for which `predicate` holds) is truthy. |
| `all(predicate=null)` | Return `true` if every element (or every for which `predicate` holds) is truthy. |
| `reduce(init, fn)` | Fold the iterable left: `fn(fn(fn(init, e0), e1), e2) …` |
| `find(predicate)` | Return the first element satisfying `predicate`, or `null`. |
| `max(comparator=null)` | Return the largest element. |
| `min(comparator=null)` | Return the smallest element. |
| `first()` | Return the first element (raises if empty). |
| `last()` | Return the last element (raises if empty). |
| `nth(idx)` | Return the element at position `idx` (raises if out of range). |
| `position(e)` | Return the index of the first occurrence of `e`, or `null`. |
| `join(sep="")` | Concatenate all elements as strings, separated by `sep`. |
| `forEach(fn)` | Call `fn(element)` for every element. |
| `collect(collector)` | Pass this iterable to `collector` and return the result. |

<pre class='runnable-snippet'>
print(iter.range(1, 6).reduce(1, |acc, n| => acc * n)) // 5! = 120
</pre>

<pre class='runnable-snippet'>
var words = ["the", "quick", "brown", "fox"]
print(words.map(|w| => w.reversed().join()).join(", "))
</pre>

`collect` is useful to feed an iterable into a constructor that expects an iterable argument,
such as `List` or `Tuple`:
<pre class='runnable-snippet'>
var squares = iter.range(5).map(|n| => n * n).collect(List)
print(squares)
</pre>

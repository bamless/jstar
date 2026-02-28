// Code that implements the logic of the demo page.
// Sets up the example buttons and the editor, as well as code execution.
window.addEventListener("load", () => {
    // Demo elements and helper functions
    const input = document.querySelector(".demo-editor");
    const output = document.querySelector(".demo-output");
    const errorLabel = document.querySelector(".demo-error-label");
    const editor = CodeJar(input, withLineNumbers(Prism.highlightElement));

    const setErrorLabel = (message) => {
        errorLabel.innerText = message;
        errorLabel.setAttribute('class', 'label label-red demo-error-label');
    }

    const clearErrorLabel = () => {
        errorLabel.innerText = '';
        errorLabel.setAttribute('class', 'demo-error-label');
    }

    const clearOutput = () => {
        output.innerText = "";
    }

    const addOutput = (out) => {
        output.innerText += out;
    }

    const addErrorOutput = (err) => {
        err = err.replace(/&/g, "&amp;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;").replace(/</g, "&lt;");
        output.innerHTML += `<span class="demo-error">${err}</span>`;
    }

    const setEditorCode = (source) => {
        clearOutput();
        addOutput('[...]');
        clearErrorLabel();
        editor.updateCode(source);
    }

    // Button functionalities
    const runButton = document.querySelector('#run');
    runButton.addEventListener('click', () => {
        clearErrorLabel();
        clearOutput();

        const res = jstar_api.run(editor.toString());
        switch (res) {
        case JSR_SYNTAX_ERROR:
            setErrorLabel('Syntax Error');
            break;
        case JSR_COMPILATION_ERROR:
            setErrorLabel('Compilation Error');
            break;
        case JSR_RUNTIME_ERROR:
            setErrorLabel('Runtime Error');
            break;
        }

        addOutput(jstar_api.out);
        addErrorOutput(jstar_api.err);
    });

    // Example buttons
    const helloWorldButton = document.querySelector('#hello-world');
    helloWorldButton.addEventListener('click', () => {
        setEditorCode(`print('Hello, World!')`);
    });

    const loopButton = document.querySelector('#loop');
    loopButton.addEventListener('click', () => {
        setEditorCode(
            `for var i = 0; i < 10; i += 1
	print('Iteration {0}' % i)
end`
        );
    });

    const quickSortButton = document.querySelector('#quick-sort');
    quickSortButton.addEventListener('click', () => {
        setEditorCode(
            `fun partition(list, low, high)
	var pivot = list[high]
	var i = low
	for var j = low; j < high; j += 1
		if list[j] <= pivot
			list[i], list[j] = list[j], list[i]
			i += 1
		end
	end
	list[i], list[high] = list[high], list[i]
	return i
end

fun quickSort(list, low, high)
	if low < high
		var p = partition(list, low, high)
		quickSort(list, low, p - 1)
		quickSort(list, p + 1, high)
	end
end

var list = [9, 1, 36, 37, 67, 45, 11, 27, 3, 5]
quickSort(list, 0, #list - 1)
print(list)`
        );
    });

    const regexButton = document.querySelector('#regex');
    regexButton.addEventListener('click', () => {
        setEditorCode(
            `import re

var message = '{lang} on {platform}!'
var formatted = re.substituteAll(message, '{(%a+)}', fun(arg)
	return { 
		'lang' : 'J*', 'platform' : 'the Web'
	}[arg]
end)

print(formatted)`
        );
    });

    const classesButton = document.querySelector("#classes");
    classesButton.addEventListener('click', () => {
        setEditorCode(
            `class Person
    construct(name, age)
        this.name = name
        this.age = age
    end

    fun getName()
        return this.name
    end

    fun showIncome()
        print("{0}'s income is unknown" % this.getName())
    end
end

class Employee is Person
    construct(name, age, income)
        super(name, age)
        this.income = income
    end

    fun showIncome()
        print("{0}'s income is {1}$" % (super.getName(), this.income))
    end
end

var john = Person("John", 20)
var alice = Employee("Alice", 34, 2000)

john.showIncome()
alice.showIncome()`
        );

    });

    const generatorsButton = document.querySelector("#generators");
    generatorsButton.addEventListener('click', () => {
        setEditorCode(
            `fun fibonacci()
	var a, b = 1, 1
	while true
		yield a
		a, b = b, a + b
	end
end

for var n in fibonacci().take(10)
	print(n)
end`
        );
    });

    // Click first button to setup first example
    helloWorldButton.click();
});

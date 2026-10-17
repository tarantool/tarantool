# TLA playbox
Specifications I write for work and fun.

# TLA+
It is a language which allows to write specifications for systems and algorithms using a mathematical-like language which is extended with bits of programming languages and temporal logic.
For studying it look Leslie Lamport's TLA+ video course and try his book *'Specifying Systems'* which can be found in this repository in `lectures/` folder.

# How to use

One way is to use a GUI which is called Toolbox. It is rather ugly and quite unfriendly. I've not seen it used much except in the video course from Leslie.

The other way is to setup a command line tool. For that download the `tla2tools.jar` file from the official repository: https://github.com/tlaplus/tlaplus/releases#latest-tla-files. Install this `.jar` file somehow. For instance, on Mac it can be saved into `/Library/Java/Extensions/`.

Then define an alias for bash: `alias tlap='java -XX:+IgnoreUnrecognizedVMOptions -XX:+UseParallelGC -cp /Library/Java/Extensions/tla2tools.jar tlc2.TLC'` in your `.bash_profile` file or wherever is prefered. Replace the path with the place where the file is really stored, if it is different from this example.

Now in a console can do: `tlap SimpleQueue.tla`. There are options which can be seen via `tlap -h` and in a bit different (somewhat extended, somewhat reduced) form here: https://lamport.azurewebsites.net/tla/current-tools.pdf.

There is no good documentation for syntax and builtin functions and modules except the video course and the book. Sometimes bits of useful info can be found here: https://learntla.com/tla. Also can ask questions in the official Google Group: https://groups.google.com/g/tlaplus.

# Links

* Latest TLA+: https://github.com/tlaplus/tlaplus/releases#latest-tla-files
* Official doc: https://lamport.azurewebsites.net/tla/current-tools.pdf.
* Unofficial doc: https://learntla.com/tla
* Google Group: https://groups.google.com/g/tlaplus.

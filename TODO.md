## 🔴 High Priority: Stability & Code Quality

* **[ ] Memory & Error Validation**
* Run comprehensive tests using **Valgrind** on WSL/Linux.
* Identify and fix memory leaks, buffer overflows, and uninitialized variables.
* Make dsdctl behave like sacd_extract if no arguments are passed
* compile on OSX

* **[ ] Standardize Logging Interface**
* Deprecate direct `fprintf(stderr, ...)` and `printf()` calls.
* Refactor all debugging lines to use the internal `sa_log()` wrapper for consistent formatting and log-level control.

## 🟡 Medium Priority: Documentation

* **[ ] Automated API Documentation**
* Configure `Doxyfile` to generate HTML/LaTeX output.

## 🟢 Low Priority: Maintenance

* **[ ] General Code Cleanup**
* Remove commented-out "dead code."
* Ensure consistent indentation and naming conventions across the codebase.

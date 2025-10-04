# Gawk extension for some approximation of vis encoding

Gawk shared library extension for vis en/decoding in a form that should be work for encoding entries in bsd mtree 
files.

Encodes non-printable, multibyte chars and a select set of printable chars as \ddd octal numbers
I.e. whitespace is encoded as the string `\040` 

The functions available are

* `vis::enc(input:str, chars_to_encode: Optional[str]) -> str `
* `vis::dec(input:str) -> str `

The optional second argument, overrides the default set of printable characters
that will be encoded.

The default set is (see https://man.netbsd.org/vis.3 ) 
```
*? [#';"&<>()|]$!^~`\ 
```
plus `\n` and `\t`

## Usage in gawk
Gawk loads extension using either: 
*  `-l <path>` 
*  `@load <libname>` looks for lib in directory specified by envvar `AWKLIBPATH`
     <name> can be with or without .so/.dll suffix 
* See https://www.gnu.org/software/gawk/manual/html_node/Loading-Shared-Libraries.html
Example: 
```bash
gawk -l bazel-bin/libgawk_vis.so '
BEGIN {
    print vis::enc("hello world")  # hello\040world
    print vis::dec("hello\040world")  # hello world
    print vis::enc("(◕‿◕)")  # \050\342\227\225\342\200\277\342\227\225\051
}' 
```
Or with `bazel run` if you don't have a recent version of gawk
```bash
 bazel run //:visgawk -- '
BEGIN {
    print vis::enc("hello world")  # hello\040world
    print vis::dec("hello\040world")  # hello world
    print vis::enc("(◕‿◕)")  # \050\342\227\225\342\200\277\342\227\225\051
}'
```

## Using in bazel 
The target `//:libgawk_vis.so` is a fileset with just the extension
 
## Building
If you just want the shared lib:
run 
```bash
bazel build //:gawk_vis
```
and the sharedlib is in `bazel-bin/libgawk_vis.so`


; ============================================================
; GlowLang Highlight Queries for Helix (Tree-sitter style)
; ============================================================
;
; Installation:
;   Copy this file to:
;   ~/.config/helix/runtime/queries/glowlang/highlights.scm
;
; Note: These are pattern-based highlights. For full tree-sitter
; support, a GlowLang tree-sitter grammar would be needed.
; These queries work with Helix's built-in regex highlighting.
;
; ============================================================

; --------------------------------------------------------
; Keywords
; --------------------------------------------------------

; Control flow
(keyword) @keyword
"when" @keyword.control.conditional
"whenever" @keyword.control.conditional
"eif" @keyword.control.conditional
"otherwise" @keyword.control.conditional
"while" @keyword.control.repeat
"until" @keyword.control.repeat
"repeat" @keyword.control.repeat
"for" @keyword.control.repeat
"each" @keyword.control.repeat
"in" @keyword.control
"stop" @keyword.control
"skip" @keyword.control
"end" @keyword.control
"times" @keyword.control
"from" @keyword.control
"to" @keyword.control

; Declarations
"func" @keyword.function
"lambda" @keyword.function
"return" @keyword.control.return
"let" @keyword.storage
"class" @keyword.storage.type
"blueprint" @keyword.storage.type
"trait" @keyword.storage.type
"extends" @keyword.storage
"use" @keyword.control.import
"spawn" @keyword.control
"action" @keyword.function
"every" @keyword.storage
"can" @keyword.storage

; Exception handling
"try" @keyword.control.exception
"catch" @keyword.control.exception
"throw" @keyword.control.exception

; I/O
"show" @keyword
"write" @keyword
"input" @keyword

; Logical operators
"and" @keyword.operator
"or" @keyword.operator
"not" @keyword.operator
"is" @keyword.operator
"is_not" @keyword.operator

; --------------------------------------------------------
; Literals
; --------------------------------------------------------

; Boolean
"true" @constant.builtin.boolean
"false" @constant.builtin.boolean

; Numbers
(integer) @constant.numeric.integer
(float) @constant.numeric.float
[0-9]+ @constant.numeric.integer
[0-9]+\.[0-9]* @constant.numeric.float

; Strings
(string) @string
"\"" @string
"'" @string

; F-strings
(fstring) @string.special

; --------------------------------------------------------
; Comments
; --------------------------------------------------------

(comment) @comment
; Single-line
"//" @comment.line
; Block
"/*" @comment.block

; --------------------------------------------------------
; Functions
; --------------------------------------------------------

; Built-in functions
((identifier) @function.builtin
  (#match? @function.builtin "^(len|type|range|append|push|pop|sort|reverse|keys|values|has|remove|merge|upper|lower|trim|find|replace|starts_with|ends_with|split)$"))

; Function definitions
(function_definition
  name: (identifier) @function)

; Function calls
(call_expression
  function: (identifier) @function.call)

; --------------------------------------------------------
; Types and Classes
; --------------------------------------------------------

; Class/blueprint definitions
(class_definition
  name: (identifier) @type)

; Trait definitions
(trait_definition
  name: (identifier) @type)

; Module names
((identifier) @module
  (#match? @module "^(os|math|fs|json|http|tensor)$"))

; --------------------------------------------------------
; Variables
; --------------------------------------------------------

; Variable declarations
(variable_declaration
  name: (identifier) @variable)

; Parameters
(parameter) @variable.parameter

; Generic identifiers
(identifier) @variable

; --------------------------------------------------------
; Operators
; --------------------------------------------------------

[
  "+"
  "-"
  "*"
  "/"
  "%"
  "**"
  "="
  "+="
  "-="
  "*="
  "/="
  "%="
  "=="
  "!="
  "<"
  ">"
  "<="
  ">="
] @operator

; --------------------------------------------------------
; Punctuation
; --------------------------------------------------------

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" "."] @punctuation.delimiter

#pragma once

// Entry point used by HorizonExamples:
//
//   HorizonExamples ocarina_dsl [--size WxH] [--output dir] [--print-slang]
//
// argc/argv are the full arguments passed to HorizonExamples; argv[1] is the
// "ocarina_dsl" mode.
int run_example_ocarina_dsl(int argc, char **argv);
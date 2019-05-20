#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This script generates nsStyleStructList.h, which contains macro invocations
# that can be used for three things:
#
# 1. To generate code for each inherited style struct.
# 2. To generate code for each reset style struct.
# 3. To generate the dependency of each style struct.

from __future__ import print_function

import math

NORMAL_DEP = ["Variables"]
COLOR_DEP = ["Color"]
LENGTH_DEP = ["Font", "Visibility"]

# List of style structs and their corresponding Check callback functions,
# if any.
STYLE_STRUCTS = [("INHERITED",) + x for x in [
    # Inherited style structs.
    ("Font",           "CheckFontCallback",     NORMAL_DEP + ["Visibility"]),
    ("Color",          "CheckColorCallback",    NORMAL_DEP),
    ("List",           "nullptr",               NORMAL_DEP + LENGTH_DEP),
    ("Text",           "CheckTextCallback",     NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Visibility",     "nullptr",               NORMAL_DEP),
    ("UserInterface",  "nullptr",               NORMAL_DEP),
    ("TableBorder",    "nullptr",               NORMAL_DEP + LENGTH_DEP),
    ("SVG",            "nullptr",               NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Variables",      "CheckVariablesCallback",[]),
]] + [("RESET",) + x for x in [
    # Reset style structs.
    ("Background",     "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Position",       "nullptr",   NORMAL_DEP + LENGTH_DEP),
    ("TextReset",      "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Display",        "nullptr",   NORMAL_DEP + LENGTH_DEP),
    ("Content",        "nullptr",   NORMAL_DEP + LENGTH_DEP),
    ("UIReset",        "nullptr",   NORMAL_DEP + LENGTH_DEP),
    ("Table",          "nullptr",   NORMAL_DEP),
    ("Margin",         "nullptr",   NORMAL_DEP + LENGTH_DEP),
    ("Padding",        "nullptr",   NORMAL_DEP + LENGTH_DEP),
    ("Border",         "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Outline",        "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("XUL",            "nullptr",   NORMAL_DEP),
    ("SVGReset",       "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Column",         "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
    ("Effects",        "nullptr",   NORMAL_DEP + LENGTH_DEP + COLOR_DEP),
]]


# ---- Generate nsStyleStructList.h ----

count = len(STYLE_STRUCTS)

# Check for problems with style struct dependencies
resolved_items = []
# This whole loop tries to sort the style structs in topological order
# according to the dependencies. A topological order exists iff there
# are no cyclic dependencies between the style structs. It resolves one
# struct each iteration, and append the resolved one to |resolved_items|.
for i in range(count):
    # This inner loop picks one style struct which does not have
    # unsolved dependencies. If nothing can be picked, then we
    # must have some cyclic dependencies.
    for j in range(count):
        _, name, _, dependencies = STYLE_STRUCTS[j]
        if name in resolved_items:
            continue
        # Check whether all dependencies of this item have been placed
        for dep in dependencies:
            if dep not in resolved_items:
                break
        else:
            resolved_items.append(name)
            break
    else:
        import sys
        print("ERROR: Cannot resolve style struct dependencies", file=sys.stderr)
        print("Resolved items:", " ".join(resolved_items), file=sys.stderr)
        unsolved_items = [name for _, name, _, _ in STYLE_STRUCTS
                          if name not in resolved_items]
        print("There exist cyclic dependencies between " +
                  "the following structs:", " ".join(unsolved_items), file=sys.stderr)
        exit(1)

def printEntry(header, i):
    if STYLE_STRUCTS[i][1] == "Variables":
        print("#ifndef STYLE_STRUCT_LIST_IGNORE_VARIABLES", file=header)
    print("STYLE_STRUCT_%s(%s, %s)" % STYLE_STRUCTS[i][:3], file=header)
    for dep in STYLE_STRUCTS[i][3]:
        print("STYLE_STRUCT_DEP(%s)" % (dep,), file=header)
    print("STYLE_STRUCT_END()", file=header)
    if STYLE_STRUCTS[i][1] == "Variables":
        print("#endif", file=header)

HEADER = """/* THIS FILE IS AUTOGENERATED BY generate-stylestructlist.py - DO NOT EDIT */

// IWYU pragma: private, include "nsStyleStructFwd.h"

/*
 * list of structs that contain the data provided by nsStyleContext, the
 * internal API for computed style data for an element
 */

/*
 * This file is intended to be used by different parts of the code, with
 * the STYLE_STRUCT macro (or the STYLE_STRUCT_INHERITED and
 * STYLE_STRUCT_RESET pair of macros) defined in different ways.
 */

#ifndef STYLE_STRUCT_INHERITED
#define STYLE_STRUCT_INHERITED(name, checkdata_cb) \\
  STYLE_STRUCT(name, checkdata_cb)
#define UNDEF_STYLE_STRUCT_INHERITED
#endif

#ifndef STYLE_STRUCT_RESET
#define STYLE_STRUCT_RESET(name, checkdata_cb) \\
  STYLE_STRUCT(name, checkdata_cb)
#define UNDEF_STYLE_STRUCT_RESET
#endif

#ifndef STYLE_STRUCT_DEP
#define STYLE_STRUCT_DEP(dep)
#define UNDEF_STYLE_STRUCT_DEP
#endif

#ifndef STYLE_STRUCT_END
#define STYLE_STRUCT_END()
#define UNDEF_STYLE_STRUCT_END
#endif

// The inherited structs are listed before the Reset structs.
// nsStyleStructID assumes this is the case, and callers other than
// nsStyleStructFwd.h that want the structs in id-order just define
// STYLE_STRUCT rather than including the file twice.

"""
FOOTER = """
#ifdef UNDEF_STYLE_STRUCT_INHERITED
#undef STYLE_STRUCT_INHERITED
#undef UNDEF_STYLE_STRUCT_INHERITED
#endif

#ifdef UNDEF_STYLE_STRUCT_RESET
#undef STYLE_STRUCT_RESET
#undef UNDEF_STYLE_STRUCT_RESET
#endif

#ifdef UNDEF_STYLE_STRUCT_DEP
#undef STYLE_STRUCT_DEP
#undef UNDEF_STYLE_STRUCT_DEP
#endif

#ifdef UNDEF_STYLE_STRUCT_END
#undef STYLE_STRUCT_END
#undef UNDEF_STYLE_STRUCT_END
#endif
"""

def main(header):
    print(HEADER, file=header)
    for i in range(count):
        printEntry(header, i)
    print(FOOTER, file=header)
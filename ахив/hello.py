#!/usr/bin/env python
# -*- coding: utf-8 -*-

from gimpfu import *

def hello(image, drawable):
    pdb.gimp_message("Hello from plugin!")

register(
    "hello_test",
    "Hello test",
    "Simple test",
    "A",
    "A",
    "2026",
    "<Toolbox>/Генерация_кнопок/Hello",
    None,
    [],
    [],
    hello
)

main()

# Edit

> **Fork Notice:** This is a fork of [Microsoft Edit](https://github.com/microsoft/edit) v1.0.0.
> The latest upstream release is v2.0.0 with additional non-release development.
> This repository is a C port of the original Rust application.

A C port of a simple editor for simple needs.

This is a C implementation that maintains compatibility with the original Microsoft Edit editor. It preserves the behavior and interface of the upstream Rust application while using a C toolchain and codebase.

This editor pays homage to the classic [MS-DOS Editor](https://en.wikipedia.org/wiki/MS-DOS_Editor), but with a modern interface and input controls similar to VS Code. The goal is to provide an accessible editor that even users largely unfamiliar with terminals can easily use.

![image](./assets/edit_hero_image.png)

## Installation

* Clone the repository
* Build from source by following the Build Instructions below
* Copy the `edit` binary to a directory in your `PATH`

## Build Instructions

* Install a C compiler (gcc, clang, or compatible)
* Ensure you have make or your platform's equivalent build tool
* Clone the repository
* Run: `make`
* For a release build: `make release`

# image-library
Developing a library to read from / write to a variety of image formats.

This library will also contain helper classes such as ones to parse zlib streams

## Available formats:
*PNG being worked on

## How to Use:
The generic interface for image encoders/decoders can be found at interface/image-stream-interface.h

The definitions for the image data structures can be found at interface/image-data-interface.h

## Unit Tests:
Within the directory for the specific format, there will be a test folder containing necessary resources as well as a test.cpp file to run.

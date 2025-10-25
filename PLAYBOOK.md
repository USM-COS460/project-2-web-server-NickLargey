## COS 460/540 - Computer Networks
# Project 2: HTTP Server
# Nick Largey

This project is written in Python on WSL.

## How to compile

- Written in python, no need to compile.

## How to run

- From the `project-2-web-server-NickLargey` folder in the terminal, run ```python3 /http_server.py -p <PORT> -r <DIR>```

## My experience with this project
The most important lesson I learned is that I still have a lot to learn about low level languages and how system programming actually works. Originally, I started this project in C but wasn't able to get my code to work (Even though I own the PRINT version of Beej's Guide to Network programming) so I switched to Python which has a much more robust networking, memory management and type system where I then followed a few blog posts...

These one's especially:
- [Joao Venture's Blog](https://joaoventura.net/blog/2017/python-webserver/)
- [Ruslan's Blog](https://ruslanspivak.com/lsbaws-part1/)

After getting the server working, I had the Co-Pilot Agent write a simple HTTP server in C and heavily comment the code in order for me to read through it to try and understand where I went wrong with my C code, which I've included in the Github repo, but IS NOT part of my graded submission. Overall, I'm fairly disappointed in what I'm handing in for this assignment. The code works and seems relatively elegant, but I don't really like how much Python abstracts from the learning process, and being able to complete all the assignments as I had challenged myself to do at the beginning of the semester would have been far more rewarding. 
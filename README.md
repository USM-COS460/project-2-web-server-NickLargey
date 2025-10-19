[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/dsrxv5H0)
## COS 460/540 - Computer Networks
# Project 2: HTTP Server

## Specification
In this assignment your goal is to create your own simple HTTP (web) server. Your server must allow (serve) files and subdirectories from a "document root" folder over a TCP port to HTTP clients (web browsers). Both the document root and TCP port must be configurable via command line parameters or a configuration file. Your server must be able to serve concurrent (simultaneous) requests as well as multiple requests over time.

If you are using a language, framework, or library that provides extensive HTTP infrastructure, you should not use that infrastructure or library. Your code should be written against the basic socket libraries provided by your language. Frameworks, like python's twisted, should **not** be used for your server code.

## Request
In an HTTP request, you must parse and utilize the request line. You may ignore any other request headers. This means you do not have to implement authentication & authorization, name based virtual hosting, and other header based HTTP features. Your server must be able to service text (html) and binary files (pictures, audio, etc..). Refer to the standard MIME types for appropriate headers that may need to be returned in the response.

```
GET /pub/WWW/TheProject.html HTTP/1.1
User-Agent: Simple-Client/0.5
-- blank line --
```

NOTE: The text `-- blank line --` is intended to show that a blank line must appear here. That is two CRLF pairs in a row, `\r\n\r\n`.

You only need to implement the `GET` request. You will not need to implement others like `POST`, `PUT`, etc.. You may implement them if you want.

## Response
In your HTTP response, you must adhere to the protocol or browsers will not work properly. Your response must contain the response line, response headers (Content-Length, Content-Type, Date, and Server), and the response data. Your response codes should be appropriate to the response itself, e.g. error and success codes as described in the [HTTP RFC 2616](http://www.ietf.org/rfc/rfc2616.txt). 

```
HTTP/1.1 200 OK
Date: Thu, 04 Feb 2010 18:50:12 GMT
Server: BobsDiscountServer/2.1
Content-Type: text/html
Content-Length: 1082

<html>
...page data...
</html>
```

You only need to return the status codes `200` for O.k., and `404` for file not found. You may implement more if you want to.

You will also have to handle multiple requests at the same time. Threading libraries and threading features of your language are the easiest way to accomplish this. There are alternatives, like forking new processes and using signal handlers. While you can use these approaches, I would caution you to avoid them for now. Stick with threads.

## Example 
The following is a complete example interaction with the server. User entered text is in **bold** font. System or server generated responses are in normal font. Note the blank line after the request and it’s single header line.

<pre>
<b>$ telnet localhost 1029
GET /index.html HTTP/1.0
Ignore-Header: I ignore you
 
</b>HTTP/1.1 200 OK
Date: Thu, 04 Feb 2010 18:50:12 GMT
Server: BobsDiscountServer/2.1
Content-Type: text/html
Content-Length: 1082

&lt;html&gt;
...
&lt;/html&gt;
</pre>

## Testing
Your web server should work with any standard web browser as a client. It should serve HTML text, images, css, etc.. This includes, Chrome, Firefox, Safari, and Internet Explorer.

## Notes
You may need to send back MIME type headers for each file type. On a Linux or macOS computer, you can use the file `/etc/mime.types` to get this information. If you are on a Microsoft Windows computer there is one buried in your Windows system directory. Feel free to just copy one from a Linux machine instead if it's easier.

A sample `www` directory is included for you to test with. It includes a few images (image/jpeg), a `site.css` (text/css) stylesheet, and an `index.html` (text/html) file. `index.html` should be served from the specified directory when no specific file in the directory is requested. E.g. when the `GET` request ends with a trailing slash character (`/`), serve up `index.html`, if it exists, from that directory.

## Protocol Specification
The full [HTTP 1.1 Protocol Specification](http://www.ietf.org/rfc/rfc2616.txt) is available and is included here by reference only.

## Additional Information
* The server must accept as a configurable parameter (on the command line) the port number to listen on.
* The server must accept as a configurable parameter (on the command line) the directory (document root) where files will be served from.
* The server must accept multiple clients and multiple requests (on their own sockets) at the same time.
* You must include the file named PLAYBOOK.md
* PLAYBOOK.md has &lt;&lt;Your name&gt;&gt;
* PLAYBOOK.md has what language you used
* PLAYBOOK.md has a brief synopsis of your experience with the assignment (1-3 paragraphs).
* PLAYBOOK.md has how to compile and execute your project.
* You must **not** include any executable binary files. Submit only code.
* You may include a script or batch file to compile and/or run your server. This must be documented in your PLAYBOOK.md if it is included.
* You must submit your project through [GitHub](http://github.com)

## Definitions
The IETF Best Practice Document RFC 2119 Key words for use in RFCs to Indicate Requirement Levels defines several keywords that are used in this assignment and throughout the course. Pay special attention to where they appear in the assignment.

Some keywords used in this assignment are as follows;

**MUST**: This word, or the terms **REQUIRED** or **SHALL**, mean that the
definition is an absolute requirement of the specification.

**SHOULD**: This word, or the adjective **RECOMMENDED**, mean that there may
exist valid reasons in particular circumstances to ignore a particular item, but
the full implications must be under.

**MAY**: This word, or the adjective **OPTIONAL**, mean that an item is truly
optional. One vendor may choose to include the item because a particular
marketplace requires it or because the vendor feels that it enhances the product
while another vendor may omit the same item.

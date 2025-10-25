#define _DEFAULT_SOURCE // Expose non-standard functions like strcasecmp, needed for cross-platform compatibility and modern features
/*
  Minimal Educational HTTP 1.0 Static File Server in C (single source file)

  Goals:
  - No frameworks or third-party libraries: uses only the C standard library and OS socket APIs.
  - Serves files and subdirectories rooted at a configurable document root.
  - Listens on a configurable TCP port.
  - Handles multiple concurrent clients using one thread per connection.
  - Very heavily commented for learning.

  Cross-platform notes:
  - Supports Linux/macOS (POSIX) and Windows via #ifdef _WIN32.
  - On Windows, compile and link with Ws2_32 (e.g., cl web_server.c /W4 /D_CRT_SECURE_NO_WARNINGS ws2_32.lib).
  - On POSIX, compile with: cc -std=c11 -Wall -Wextra -O2 -pthread -o web_server web_server.c

  Usage:
    web_server -r <root_dir> -p <port>
    web_server -c <config_file>
    Command line args override config file values if both are supplied.

  Config file format (simple key=value, whitespace ignored, lines starting with # are comments):
    root=/path/to/www
    port=8080

  Supported features:
  - Methods: GET and HEAD
  - Basic URL decoding and path normalization to prevent directory traversal
  - MIME type by extension (basic map)
  - Directory listing (auto-index) if no index.html is present
  - Simple logging to stdout
  - Connection: close after each response (HTTP/1.0 style)
*/

#include <sys/types.h>        // Provides basic system data types
#include <sys/socket.h>       // Provides socket-related functions and structures
#include <sys/stat.h>         // Provides file status functions and structures
#include <sys/sendfile.h>     // Provides sendfile function for efficient file transfer
#include <netinet/in.h>       // Provides internet address family structures
#include <arpa/inet.h>        // Provides functions for manipulating IP addresses
#include <netdb.h>            // Provides network database operations
#include <dirent.h>           // Provides directory entry structures and functions
#include <pthread.h>          // Provides POSIX thread functions for concurrency
#include <unistd.h>           // Provides POSIX operating system API
#include <fcntl.h>            // Provides file control options
#include <signal.h>           // Provides signal handling functions
#include <stdarg.h>           // Provides support for variable argument lists
typedef int sock_t;           // Defines a custom type for socket descriptors for cross-platform compatibility
#define INVALID_SOCKET (-1)   // Defines a value for an invalid socket
#define SOCKET_ERROR (-1)     // Defines a value for a socket error
#define CLOSESOCK(s) close(s) // Defines a macro for closing a socket

#include <stdio.h>  // Provides standard input/output functions
#include <stdlib.h> // Provides general utility functions
#include <string.h> // Provides string manipulation functions
#include <time.h>   // Provides time and date functions
#include <ctype.h>  // Provides character handling functions
#include <errno.h>  // Provides access to error numbers

#ifndef PATH_MAX      // If PATH_MAX is not defined
#define PATH_MAX 4096 // define it to a common value to ensure buffer sizes are adequate for file paths
#endif                // End of PATH_MAX definition

#define RECV_BUF_SIZE 8192  // Defines the maximum size of the request header we'll parse
#define SEND_BUF_SIZE 16384 // Defines the chunk size when sending files
#define SMALL_BUF 256       // Defines a small buffer size for short strings
#define BIG_BUF 8192        // Defines a large buffer size for formatted strings
#define MAX_MIME_LEN 64     // Defines the maximum length of a MIME type string

// Server configuration container
typedef struct              // Defines a structure to hold the server's configuration
{                           // Start of server_config_t structure definition
  char root[PATH_MAX];      // The root directory as provided by the user
  char root_real[PATH_MAX]; // The canonical absolute path to the root, used for security checks
  int port;                 // The port number to listen on
} server_config_t;          // End of server_config_t structure definition

typedef struct                  // Defines a structure to hold client context information
{                               // Start of client_ctx_t structure definition
  sock_t client;                // The client's socket descriptor
  struct sockaddr_storage addr; // The client's address information
  socklen_t addrlen;            // The length of the client's address structure
  server_config_t *cfg;         // A pointer to the server's configuration
} client_ctx_t;                 // End of client_ctx_t structure definition

/* Utility: trim leading/trailing whitespace from a mutable C string in-place */
static char *strtrim(char *s)                     // Defines a function to trim whitespace from a string
{                                                 // Start of strtrim function body
  char *end;                                      // Declares a pointer to the end of the string
  while (*s && isspace((unsigned char)*s))        // Loop while the current character is whitespace
    s++;                                          // Move the start pointer forward
  if (*s == 0)                                    // If the string is empty or contains only whitespace
    return s;                                     // return the pointer to the null terminator
  end = s + strlen(s) - 1;                        // Set the end pointer to the last character of the string
  while (end > s && isspace((unsigned char)*end)) // Loop while the character at the end is whitespace
    end--;                                        // Move the end pointer backward
  end[1] = '\0';                                  // Null-terminate the trimmed string
  return s;                                       // Return the pointer to the trimmed string
} // End of strtrim function body

/* Utility: case-insensitive string prefix check
   Returns 1 if 's' begins with 'prefix' (case-insensitive), else 0 */
static int stristartswith(const char *s, const char *prefix)    // Defines a function for case-insensitive prefix checking
{                                                               // Start of stristartswith function body
  size_t n = strlen(prefix);                                    // Get the length of the prefix
  for (size_t i = 0; i < n; i++)                                // Loop through each character of the prefix
  {                                                             // Start of for loop body
    char a = s[i], b = prefix[i];                               // Get the characters from the string and prefix
    if (!a)                                                     // If the main string is shorter than the prefix
      return 0;                                                 // it can't start with the prefix
    if (tolower((unsigned char)a) != tolower((unsigned char)b)) // If the characters don't match (case-insensitively)
      return 0;                                                 // the string does not start with the prefix
  } // End of for loop body
  return 1; // If the loop completes, the string starts with the prefix
} // End of stristartswith function body

/* Encode minimal HTML entities for display safety (directory listing)
   Replaces &, <, >, " with their entities. Returns a newly allocated string */
static char *html_escape(const char *in) // Defines a function to escape HTML special characters
{                                        // Start of html_escape function body
  size_t extra = 0;                      // Initialize a counter for the extra space needed for escaped characters
  for (const char *p = in; *p; ++p)      // Loop through each character of the input string
  {                                      // Start of for loop body
    switch (*p)                          // Check the current character
    {                                    // Start of switch statement
    case '&':                            // If the character is an ampersand
      extra += 4;                        // add space for "&amp;"
      break;                             // Exit the switch
    case '<':                            // If the character is a less-than sign
      extra += 3;                        // add space for "&lt;"
      break;                             // Exit the switch
    case '>':                            // If the character is a greater-than sign
      extra += 3;                        // add space for "&gt;"
      break;                             // Exit the switch
    case '"':                            // If the character is a double quote
      extra += 5;                        // add space for "&quot;"
      break;                             // Exit the switch
    default:                             // For any other character
      break;                             // do nothing
    } // End of switch statement
  } // End of for loop body
  size_t len = strlen(in);                     // Get the original length of the string
  char *out = (char *)malloc(len + extra + 1); // Allocate memory for the new string, including extra space and a null terminator
  if (!out)                                    // If memory allocation fails
    return NULL;                               // return NULL
  char *q = out;                               // Create a pointer to the beginning of the output string
  for (const char *p = in; *p; ++p)            // Loop through the input string again
  {                                            // Start of for loop body
    switch (*p)                                // Check the current character
    {                                          // Start of switch statement
    case '&':                                  // If it's an ampersand
      memcpy(q, "&amp;", 5);                   // copy "&amp;" to the output
      q += 5;                                  // Move the output pointer forward
      break;                                   // Exit the switch
    case '<':                                  // If it's a less-than sign
      memcpy(q, "&lt;", 4);                    // copy "&lt;" to the output
      q += 4;                                  // Move the output pointer forward
      break;                                   // Exit the switch
    case '>':                                  // If it's a greater-than sign
      memcpy(q, "&gt;", 4);                    // copy "&gt;" to the output
      q += 4;                                  // Move the output pointer forward
      break;                                   // Exit the switch
    case '"':                                  // If it's a double quote
      memcpy(q, "&quot;", 6);                  // copy "&quot;" to the output
      q += 6;                                  // Move the output pointer forward
      break;                                   // Exit the switch
    default:                                   // For any other character
      *q++ = *p;                               // copy it directly to the output
      break;                                   // Exit the switch
    } // End of switch statement
  } // End of for loop body
  *q = '\0';  // Null-terminate the output string
  return out; // Return the newly created escaped string
} // End of html_escape function body

/* URL-decode a path (handles %XX and +->space)
   Decodes in-place; returns 0 on success, -1 if invalid percent-encoding */
static int url_decode(char *s)                                              // Defines a function to decode a URL-encoded string
{                                                                           // Start of url_decode function body
  char *o = s;                                                              // Create a pointer to the output position in the string (for in-place decoding)
  for (char *p = s; *p; ++p)                                                // Loop through each character of the input string
  {                                                                         // Start of for loop body
    if (*p == '%')                                                          // If the character is a percent sign, it indicates an encoded character
    {                                                                       // Start of if block
      if (!isxdigit((unsigned char)p[1]) || !isxdigit((unsigned char)p[2])) // Check if the next two characters are valid hex digits
        return -1;                                                          // If not, the encoding is invalid, so return an error
      char hex[3] = {p[1], p[2], 0};                                        // Create a null-terminated string from the two hex digits
      *o++ = (char)strtol(hex, NULL, 16);                                   // Convert the hex string to a character and write it to the output
      p += 2;                                                               // Skip the two hex digits in the input string
    } // End of if block
    else if (*p == '+') // If the character is a plus sign
    {                   // Start of else if block
      *o++ = ' ';       // convert it to a space
    } // End of else if block
    else         // For any other character
    {            // Start of else block
      *o++ = *p; // copy it directly to the output
    } // End of else block
  } // End of for loop body
  *o = '\0'; // Null-terminate the decoded string
  return 0;  // Return 0 to indicate success
} // End of url_decode function body

/* Determine if a file path exists and whether it's a directory */
static int path_stat_isdir(const char *path, int *is_dir, long long *file_size) // Defines a function to get file status
{                                                                               // Start of path_stat_isdir function body
  struct stat st;                                                               // Declare a stat structure to hold file information
  if (stat(path, &st) != 0)                                                     // Call the stat function to get information about the file
    return -1;                                                                  // If stat fails, return an error
  if (is_dir)                                                                   // If the is_dir pointer is not null
    *is_dir = S_ISDIR(st.st_mode);                                              // set its value to indicate whether the path is a directory
  if (file_size)                                                                // If the file_size pointer is not null
    *file_size = (long long)st.st_size;                                         // set its value to the size of the file
  return 0;                                                                     // Return 0 to indicate success
} // End of path_stat_isdir function body

/* Canonicalize an absolute path. Returns 0 on success
   Windows: _fullpath; POSIX: realpath
   The input must exist on disk for canonicalization to succeed */
static int canonicalize_path(const char *in, char *out, size_t out_sz) // Defines a function to get the canonical path of a file
{                                                                      // Start of canonicalize_path function body
  char *r = realpath(in, NULL);                                        // Use realpath to resolve the absolute path, handling ".." and "." components
  if (!r)                                                              // If realpath fails (e.g., the path doesn't exist)
    return -1;                                                         // return an error
  if (strlen(r) + 1 > out_sz)                                          // Check if the resulting path fits in the output buffer
  {                                                                    // Start of if block
    free(r);                                                           // Free the memory allocated by realpath
    return -1;                                                         // Return an error because the buffer is too small
  } // End of if block
  strcpy(out, r); // Copy the canonical path to the output buffer
  free(r);        // Free the memory allocated by realpath
  return 0;       // Return 0 to indicate success
} // End of canonicalize_path function body

/* Join root + relative request path into a filesystem path safely
   - request_path should start with '/' (HTTP path)
   - Performs URL-decoding and normalization
   - Ensures the final canonical path stays under cfg->root_real (prevents directory traversal)
   - Returns 0 on success and writes the absolute path to out_path */
static int map_url_to_fs(const server_config_t *cfg, const char *request_path, char *out_path, size_t out_sz) // Defines a function to map a URL path to a filesystem path
{                                                                                                             // Start of map_url_to_fs function body
  // Work on a mutable copy of the request path (strip query, fragment)
  char path[PATH_MAX];                           // Declare a buffer to hold a mutable copy of the request path
  strncpy(path, request_path, sizeof(path) - 1); // Copy the request path into the buffer
  path[sizeof(path) - 1] = 0;                    // Ensure the buffer is null-terminated
  // Cut off query string and fragment
  char *q = strchr(path, '?'); // Find the start of the query string
  if (q)                       // If a query string exists
    *q = 0;                    // null-terminate the path before it
  char *h = strchr(path, '#'); // Find the start of the fragment
  if (h)                       // If a fragment exists
    *h = 0;                    // null-terminate the path before it
  // URL decode
  if (url_decode(path) != 0) // Decode any URL-encoded characters in the path
    return -1;               // If decoding fails, return an error

  // Translate '/' to platform separator for FS join
  const char sep = '/'; // Define the path separator for the filesystem

  // Build a tentative path: root + SEP + path (without leading '/')
  char rel[PATH_MAX]; // Declare a buffer for the relative path
  // Remove leading '/'
  const char *p = path; // Create a pointer to the start of the path
  while (*p == '/')     // While the path has leading slashes
    p++;                // move the pointer forward

  // Disallow dangerous components like ".." when splitting; still we will validate with canonicalization
  // Create root + sep + rel
  if (snprintf(rel, sizeof(rel), "%s%c%s", cfg->root, sep, p) >= (int)sizeof(rel)) // Construct the full tentative path
    return -1;                                                                     // If the path is too long, return an error

  // If the tentative path is a directory, canonicalize that; else, canonicalize the file path
  // Canonicalization requires the path to exist; but we can canonicalize parent directory then append last segment
  // Instead, attempt canonicalize directly; if it fails, return error and let caller send 404
  if (canonicalize_path(rel, out_path, out_sz) != 0) // Get the canonical path of the constructed path
    return -2;                                       // If canonicalization fails, return a "not found" error

  // Ensure out_path is under root_real
  size_t rlen = strlen(cfg->root_real);            // Get the length of the canonical root path
  if (rlen > 0 && cfg->root_real[rlen - 1] != '/') // If the root path is not just "/"
  {                                                // Start of if block
    // Compare with root + '/'
    char root_with_sep[PATH_MAX];                                                                              // Declare a buffer for the root path with a trailing slash
    snprintf(root_with_sep, sizeof(root_with_sep), "%s/", cfg->root_real);                                     // Add a trailing slash to the root path
    if (strncmp(out_path, root_with_sep, strlen(root_with_sep)) != 0 && strcmp(out_path, cfg->root_real) != 0) // Check if the requested path is outside the document root
      return -3;                                                                                               // If it is, return a "forbidden" error
  } // End of if block
  else                                                // If the root path is "/"
  {                                                   // Start of else block
    if (strncmp(out_path, cfg->root_real, rlen) != 0) // Check if the requested path is outside the document root
      return -3;                                      // If it is, return a "forbidden" error
  } // End of else block
  return 0; // Return 0 to indicate success
} // End of map_url_to_fs function body

/* Format current time in RFC 1123 format for HTTP Date header */
static void http_date_now(char out[SMALL_BUF])                 // Defines a function to get the current time in HTTP date format
{                                                              // Start of http_date_now function body
  time_t t = time(NULL);                                       // Get the current time as a time_t value
  struct tm tmv;                                               // Declare a tm structure to hold the broken-down time
  gmtime_r(&t, &tmv);                                          // Convert the time_t value to a UTC time structure
  strftime(out, SMALL_BUF, "%a, %d %b %Y %H:%M:%S GMT", &tmv); // Format the time into the specified string format
} // End of http_date_now function body

/* Guess MIME type based on file extension. Falls back to application/octet-stream */
static void guess_mime_type(const char *path, char out[MAX_MIME_LEN]) // Defines a function to guess the MIME type from a file extension
{                                                                     // Start of guess_mime_type function body
  const char *ext = strrchr(path, '.');                               // Find the last occurrence of a period in the path
  if (!ext)                                                           // If there is no extension
  {                                                                   // Start of if block
    strcpy(out, "application/octet-stream");                          // default to a generic binary stream type
    return;                                                           // Exit the function
  } // End of if block
  ext++; // Move the pointer past the period to the start of the extension
  // Common simple types
  if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm"))      // If the extension is "html" or "htm"
    strcpy(out, "text/html; charset=utf-8");                    // set the MIME type to HTML
  else if (!strcasecmp(ext, "css"))                             // If the extension is "css"
    strcpy(out, "text/css; charset=utf-8");                     // set the MIME type to CSS
  else if (!strcasecmp(ext, "js"))                              // If the extension is "js"
    strcpy(out, "application/javascript; charset=utf-8");       // set the MIME type to JavaScript
  else if (!strcasecmp(ext, "json"))                            // If the extension is "json"
    strcpy(out, "application/json; charset=utf-8");             // set the MIME type to JSON
  else if (!strcasecmp(ext, "txt"))                             // If the extension is "txt"
    strcpy(out, "text/plain; charset=utf-8");                   // set the MIME type to plain text
  else if (!strcasecmp(ext, "png"))                             // If the extension is "png"
    strcpy(out, "image/png");                                   // set the MIME type to PNG image
  else if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) // If the extension is "jpg" or "jpeg"
    strcpy(out, "image/jpeg");                                  // set the MIME type to JPEG image
  else if (!strcasecmp(ext, "gif"))                             // If the extension is "gif"
    strcpy(out, "image/gif");                                   // set the MIME type to GIF image
  else if (!strcasecmp(ext, "svg"))                             // If the extension is "svg"
    strcpy(out, "image/svg+xml");                               // set the MIME type to SVG image
  else if (!strcasecmp(ext, "ico"))                             // If the extension is "ico"
    strcpy(out, "image/x-icon");                                // set the MIME type to icon
  else if (!strcasecmp(ext, "pdf"))                             // If the extension is "pdf"
    strcpy(out, "application/pdf");                             // set the MIME type to PDF
  else if (!strcasecmp(ext, "mp4"))                             // If the extension is "mp4"
    strcpy(out, "video/mp4");                                   // set the MIME type to MP4 video
  else                                                          // For any other extension
    strcpy(out, "application/octet-stream");                    // default to a generic binary stream type
} // End of guess_mime_type function body

/* Send all bytes in buffer reliably over a blocking socket. Returns 0 on success, -1 on error */
static int send_all(sock_t s, const void *buf, size_t len) // Defines a function to send all data in a buffer over a socket
{                                                          // Start of send_all function body
  const char *p = (const char *)buf;                       // Create a pointer to the start of the buffer
  while (len > 0)                                          // Loop until all bytes have been sent
  {                                                        // Start of while loop body
    ssize_t n = send(s, p, len, 0);                        // Send data from the buffer over the socket
    if (n <= 0)                                            // If send returns an error or 0
      return -1;                                           // return an error
    p += n;                                                // Move the buffer pointer forward by the number of bytes sent
    len -= (size_t)n;                                      // Decrease the remaining length by the number of bytes sent
  } // End of while loop body
  return 0; // Return 0 to indicate success
} // End of send_all function body

/* Send a formatted string. Returns 0 on success, -1 on error */
static int sendf(sock_t s, const char *fmt, ...)                           // Defines a function to send a formatted string over a socket
{                                                                          // Start of sendf function body
  char buf[BIG_BUF];                                                       // Declare a buffer to hold the formatted string
  va_list ap;                                                              // Declare a variable argument list
  va_start(ap, fmt);                                                       // Initialize the variable argument list
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);                            // Format the string into the buffer
  va_end(ap);                                                              // End the variable argument list
  if (n < 0)                                                               // If formatting fails
    return -1;                                                             // return an error
  size_t tosend = (size_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf)); // Determine the number of bytes to send
  return send_all(s, buf, tosend);                                         // Send the formatted string over the socket
} // End of sendf function body

/* Emit a simple error response with given status and message */
static void send_error(sock_t s, int status, const char *reason, const char *detail) // Defines a function to send an HTTP error response
{                                                                                    // Start of send_error function body
  char date[SMALL_BUF];                                                              // Declare a buffer for the date string
  http_date_now(date);                                                               // Get the current date in HTTP format
  char body[BIG_BUF];                                                                // Declare a buffer for the HTML body of the error page
  int blen = snprintf(body, sizeof(body),                                            // Format the HTML body
                      "<!doctype html><html><head><meta charset=\"utf-8\"><title>%d %s</title></head>"
                      "<body><h1>%d %s</h1><p>%s</p></body></html>",
                      status, reason, status, reason, detail ? detail : "");
  if (blen < 0)                                           // If formatting fails
    blen = 0;                                             // set the length to 0
  sendf(s, "HTTP/1.0 %d %s\r\n", status, reason);         // Send the HTTP status line
  sendf(s, "Date: %s\r\n", date);                         // Send the Date header
  sendf(s, "Server: c-mini/1.0\r\n");                     // Send the Server header
  sendf(s, "Content-Type: text/html; charset=utf-8\r\n"); // Send the Content-Type header
  sendf(s, "Content-Length: %d\r\n", blen);               // Send the Content-Length header
  sendf(s, "Connection: close\r\n\r\n");                  // Send the Connection header and the end of headers
  send_all(s, body, (size_t)blen);                        // Send the HTML body
} // End of send_error function body

/* Helper to reallocate the HTML buffer if more space is needed. Returns 1 on success */
static int reserve_html_buf(char **html, size_t *cap, size_t len, size_t need) // Defines a helper function to manage buffer reallocation
{                                                                              // Start of reserve_html_buf function body
  if (len + need + 1 > *cap)                                                   // If the required size exceeds the current capacity
  {                                                                            // Start of if block
    size_t ncap = *cap;                                                        // Start with the current capacity
    while (len + need + 1 > ncap)                                              // While the required size is still too large
      ncap *= 2;                                                               // double the capacity
    char *nh = (char *)realloc(*html, ncap);                                   // Reallocate the buffer with the new capacity
    if (!nh)                                                                   // If reallocation fails
      return 0;                                                                // return 0 to indicate failure
    *html = nh;                                                                // Update the buffer pointer
    *cap = ncap;                                                               // Update the capacity variable
  } // End of if block
  return 1; // Return 1 to indicate success
} // End of reserve_html_buf function body

/* Produce an HTML directory listing for 'dirpath' and send it. Returns 0 on success */
static int send_dir_listing(sock_t s, const char *url_path, const char *dirpath) // Defines a function to send an HTML directory listing
{                                                                                // Start of send_dir_listing function body
  char date[SMALL_BUF];                                                          // Declare a buffer for the date string
  http_date_now(date);                                                           // Get the current date in HTTP format

  // Build HTML into a dynamically growing buffer
  size_t cap = 8192, len = 0;                                     // Initialize capacity and length for the HTML buffer
  char *html = (char *)malloc(cap);                               // Allocate the initial HTML buffer
  if (!html)                                                      // If allocation fails
  {                                                               // Start of if block
    send_error(s, 500, "Internal Server Error", "Out of memory"); // send a 500 error
    return -1;                                                    // Return an error
  } // End of if block

  const char *title = "Index of ";                          // Define the title prefix
  char *esc_title = html_escape(url_path ? url_path : "/"); // Escape the URL path for safe display
  if (!esc_title)                                           // If escaping fails
    esc_title = strdup("/");                                // use a default value

  // Header
  {                                      // Start of header block
    char head[1024];                     // Declare a buffer for the HTML header
    int n = snprintf(head, sizeof(head), // Format the HTML header
                     "<!doctype html><html><head><meta charset=\"utf-8\">"
                     "<title>%s%s</title>"
                     "<style>body{font-family:system-ui,Segoe UI,Arial,sans-serif;margin:1em auto;max-width:900px}"
                     "a{text-decoration:none;color:#05c}a:hover{text-decoration:underline}"
                     "table{border-collapse:collapse;width:100%%}th,td{padding:4px 8px;border-bottom:1px solid #eee;text-align:left}"
                     "</style></head><body><h1>%s%s</h1><table><tr><th>Name</th><th>Type</th></tr>",
                     title, esc_title, title, esc_title);
    if (!reserve_html_buf(&html, &cap, len, (size_t)n))             // Ensure the buffer is large enough
    {                                                               // Start of if block
      free(html);                                                   // Free the HTML buffer
      free(esc_title);                                              // Free the escaped title
      send_error(s, 500, "Internal Server Error", "Out of memory"); // Send a 500 error
      return -1;                                                    // Return an error
    } // End of if block
    memcpy(html + len, head, (size_t)n); // Copy the header to the HTML buffer
    len += (size_t)n;                    // Update the length
  } // End of header block

  // Parent directory entry if not root
  if (url_path && strcmp(url_path, "/") != 0)  // If the current path is not the root
  {                                            // Start of if block
    const char *last = strrchr(url_path, '/'); // find the last slash
    char parent[PATH_MAX];                     // Declare a buffer for the parent path
    if (last && last != url_path)              // If a slash is found and it's not the first character
    {                                          // Start of if block
      size_t plen = (size_t)(last - url_path); // calculate the length of the parent path
      if (plen == 0)                           // If the parent is the root
        plen = 1;                              // set the length to 1
      memcpy(parent, url_path, plen);          // Copy the parent path
      parent[plen] = '\0';                     // Null-terminate it
    } // End of if block
    else                   // Otherwise
    {                      // Start of else block
      strcpy(parent, "/"); // the parent is the root
    } // End of else block
    char row[512];                                                                                           // Declare a buffer for the table row
    int n = snprintf(row, sizeof(row), "<tr><td><a href=\"%s\">..</a></td><td>directory</td></tr>", parent); // Format the ".." entry
    if (!reserve_html_buf(&html, &cap, len, (size_t)n))                                                      // Ensure the buffer is large enough
    {                                                                                                        // Start of if block
      free(html);                                                                                            // Free the HTML buffer
      free(esc_title);                                                                                       // Free the escaped title
      send_error(s, 500, "Internal Server Error", "Out of memory");                                          // Send a 500 error
      return -1;                                                                                             // Return an error
    } // End of if block
    memcpy(html + len, row, (size_t)n); // Copy the row to the HTML buffer
    len += (size_t)n;                   // Update the length
  } // End of if block

  // POSIX directory enumeration
  DIR *d = opendir(dirpath);                                                 // Open the directory
  if (!d)                                                                    // If opening fails
  {                                                                          // Start of if block
    free(html);                                                              // Free the HTML buffer
    free(esc_title);                                                         // Free the escaped title
    send_error(s, 500, "Internal Server Error", "Unable to read directory"); // Send a 500 error
    return -1;                                                               // Return an error
  } // End of if block
  struct dirent *de;                                                                         // Declare a directory entry structure
  while ((de = readdir(d)) != NULL)                                                          // Loop through each entry in the directory
  {                                                                                          // Start of while loop body
    const char *name = de->d_name;                                                           // Get the name of the entry
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)                                   // Skip the "." and ".." entries
      continue;                                                                              // Continue to the next entry
    char childpath[PATH_MAX];                                                                // Declare a buffer for the child path
    snprintf(childpath, sizeof(childpath), "%s/%s", dirpath, name);                          // Construct the full path of the child entry
    int isdir = 0;                                                                           // Initialize a flag to indicate if the entry is a directory
    if (path_stat_isdir(childpath, &isdir, NULL) != 0)                                       // Get the status of the child entry
      isdir = 0;                                                                             // If stat fails, assume it's not a directory
    char *esc = html_escape(name);                                                           // Escape the name for safe display
    if (!esc)                                                                                // If escaping fails
      continue;                                                                              // skip this entry
    char href[PATH_MAX];                                                                     // Declare a buffer for the hyperlink
    if (url_path && url_path[0] && strcmp(url_path, "/") != 0)                               // If the current path is not the root
      snprintf(href, sizeof(href), "%s/%s%s", url_path, name, isdir ? "/" : "");             // construct the relative hyperlink
    else                                                                                     // Otherwise
      snprintf(href, sizeof(href), "/%s%s", name, isdir ? "/" : "");                         // construct the absolute hyperlink
    char row[1024];                                                                          // Declare a buffer for the table row
    int n = snprintf(row, sizeof(row), "<tr><td><a href=\"%s\">%s</a></td><td>%s</td></tr>", // Format the table row
                     href, esc, isdir ? "directory" : "file");
    free(esc);                                                      // Free the escaped name
    if (!reserve_html_buf(&html, &cap, len, (size_t)n))             // Ensure the buffer is large enough
    {                                                               // Start of if block
      closedir(d);                                                  // Close the directory
      free(html);                                                   // Free the HTML buffer
      free(esc_title);                                              // Free the escaped title
      send_error(s, 500, "Internal Server Error", "Out of memory"); // Send a 500 error
      return -1;                                                    // Return an error
    } // End of if block
    memcpy(html + len, row, (size_t)n); // Copy the row to the HTML buffer
    len += (size_t)n;                   // Update the length
  } // End of while loop body
  closedir(d); // Close the directory

  // Footer
  {                                                                 // Start of footer block
    const char *foot = "</table></body></html>";                    // Define the HTML footer
    size_t n = strlen(foot);                                        // Get the length of the footer
    if (!reserve_html_buf(&html, &cap, len, n))                     // Ensure the buffer is large enough
    {                                                               // Start of if block
      free(html);                                                   // Free the HTML buffer
      free(esc_title);                                              // Free the escaped title
      send_error(s, 500, "Internal Server Error", "Out of memory"); // Send a 500 error
      return -1;                                                    // Return an error
    } // End of if block
    memcpy(html + len, foot, n); // Copy the footer to the HTML buffer
    len += n;                    // Update the length
  } // End of footer block

  // Send response
  sendf(s, "HTTP/1.0 200 OK\r\n");                        // Send the HTTP status line
  sendf(s, "Date: %s\r\n", date);                         // Send the Date header
  sendf(s, "Server: c-mini/1.0\r\n");                     // Send the Server header
  sendf(s, "Content-Type: text/html; charset=utf-8\r\n"); // Send the Content-Type header
  sendf(s, "Content-Length: %zu\r\n", len);               // Send the Content-Length header
  sendf(s, "Connection: close\r\n\r\n");                  // Send the Connection header and the end of headers
  int rc = send_all(s, html, len);                        // Send the HTML body
  free(html);                                             // Free the HTML buffer
  free(esc_title);                                        // Free the escaped title
  return rc;                                              // Return the result of the send operation
} // End of send_dir_listing function body

/* Attempt to serve a file (GET or HEAD). Streams file in chunks
   Returns 0 on success, -1 on error */
static int send_file(sock_t s, const char *filepath, int is_head)             // Defines a function to send a file
{                                                                             // Start of send_file function body
  char date[SMALL_BUF];                                                       // Declare a buffer for the date string
  http_date_now(date);                                                        // Get the current date in HTTP format
  long long fsize = 0;                                                        // Initialize the file size to 0
  int isdir = 0;                                                              // Initialize a flag to indicate if the path is a directory
  if (path_stat_isdir(filepath, &isdir, &fsize) != 0 || isdir)                // Get the status of the file
  {                                                                           // Start of if block
    send_error(s, 404, "Not Found", "The requested resource was not found."); // If it's a directory or doesn't exist, send a 404 error
    return -1;                                                                // Return an error
  } // End of if block

  FILE *f = fopen(filepath, "rb");                                            // Open the file in binary read mode
  if (!f)                                                                     // If opening fails
  {                                                                           // Start of if block
    send_error(s, 404, "Not Found", "The requested resource was not found."); // send a 404 error
    return -1;                                                                // Return an error
  } // End of if block

  char mime[MAX_MIME_LEN];         // Declare a buffer for the MIME type
  guess_mime_type(filepath, mime); // Guess the MIME type from the file path

  sendf(s, "HTTP/1.0 200 OK\r\n");             // Send the HTTP status line
  sendf(s, "Date: %s\r\n", date);              // Send the Date header
  sendf(s, "Server: c-mini/1.0\r\n");          // Send the Server header
  sendf(s, "Content-Type: %s\r\n", mime);      // Send the Content-Type header
  sendf(s, "Content-Length: %lld\r\n", fsize); // Send the Content-Length header
  sendf(s, "Connection: close\r\n\r\n");       // Send the Connection header and the end of headers

  if (!is_head)                                     // If the request method is not HEAD
  {                                                 // Start of if block
    char buf[SEND_BUF_SIZE];                        // declare a buffer for sending the file content
    size_t n;                                       // Declare a variable to hold the number of bytes read
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) // Loop while there is data to read from the file
    {                                               // Start of while loop body
      if (send_all(s, buf, n) != 0)                 // Send the read data over the socket
      {                                             // Start of if block
        fclose(f);                                  // Close the file
        return -1;                                  // Return an error if sending fails
      } // End of if block
    } // End of while loop body
  } // End of if block
  fclose(f); // Close the file
  return 0;  // Return 0 to indicate success
} // End of send_file function body

/* Parse a single HTTP request from the client socket
   - Reads until CRLFCRLF or buffer full
   - Extracts method, path, version
   - Ignores headers (except we could examine Host etc. for advanced features)
   Returns 0 on success; -1 on error */
static int read_http_request(sock_t s, char *method, size_t msz, char *path, size_t psz, char *version, size_t vsz) // Defines a function to read and parse an HTTP request
{                                                                                                                   // Start of read_http_request function body
  char buf[RECV_BUF_SIZE];                                                                                          // Declare a buffer to receive the request
  size_t used = 0;                                                                                                  // Initialize the number of bytes used in the buffer

  // Simple blocking read with a soft timeout can be added; for simplicity we omit it
  for (;;)                                                                                    // Loop indefinitely to read from the socket
  {                                                                                           // Start of for loop body
    if (used >= sizeof(buf))                                                                  // If the buffer is full
      break;                                                                                  // stop reading
    ssize_t n = recv(s, buf + used, sizeof(buf) - used, 0);                                   // Receive data from the socket
    if (n <= 0)                                                                               // If recv returns an error or 0
      break;                                                                                  // stop reading
    used += (size_t)n;                                                                        // Increment the number of bytes used
    if (used >= 4)                                                                            // If we have at least 4 bytes
    {                                                                                         // Start of if block
      for (size_t i = 3; i < used; i++)                                                       // loop through the buffer to find the end of headers
      {                                                                                       // Start of for loop body
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n') // Check for the CRLFCRLF sequence
        {                                                                                     // Start of if block
          used = i + 1;                                                                       // Set the used size to the end of the headers
          goto parse;                                                                         // Jump to the parsing section
        } // End of if block
      } // End of for loop body
    } // End of if block
  } // End of for loop body
parse:           // Label for the parsing section
  if (used == 0) // If no data was received
    return -1;   // return an error

  // Null-terminate for tokenization
  buf[(used < sizeof(buf)) ? used : sizeof(buf) - 1] = '\0'; // Null-terminate the received data

  // First line: METHOD SP PATH SP VERSION
  char *line_end = strstr(buf, "\r\n"); // Find the end of the first line
  if (!line_end)                        // If not found
    return -1;                          // the request is malformed
  *line_end = '\0';                     // Null-terminate the first line

  char *sp1 = strchr(buf, ' ');     // Find the first space
  if (!sp1)                         // If not found
    return -1;                      // the request is malformed
  *sp1 = '\0';                      // Null-terminate the method
  char *sp2 = strchr(sp1 + 1, ' '); // Find the second space
  if (!sp2)                         // If not found
    return -1;                      // the request is malformed
  *sp2 = '\0';                      // Null-terminate the path

  strncpy(method, buf, msz - 1);      // Copy the method to the output buffer
  method[msz - 1] = 0;                // Ensure it's null-terminated
  strncpy(path, sp1 + 1, psz - 1);    // Copy the path to the output buffer
  path[psz - 1] = 0;                  // Ensure it's null-terminated
  strncpy(version, sp2 + 1, vsz - 1); // Copy the version to the output buffer
  version[vsz - 1] = 0;               // Ensure it's null-terminated

  return 0; // Return 0 to indicate success
} // End of read_http_request function body

/* Handle one client connection: parse request, map path, serve file or directory listing */
static void handle_client(client_ctx_t *ctx)                                                                     // Defines the main function to handle a client connection
{                                                                                                                // Start of handle_client function body
  char method[16], path[PATH_MAX], version[16];                                                                  // Declare buffers for the request components
  if (read_http_request(ctx->client, method, sizeof(method), path, sizeof(path), version, sizeof(version)) != 0) // Read and parse the HTTP request
  {                                                                                                              // Start of if block
    // Cannot parse request; close silently
    return; // If parsing fails, simply close the connection
  } // End of if block

  // Log request line
  char addrstr[NI_MAXHOST];                                                                                    // Declare a buffer for the client's address string
  addrstr[0] = 0;                                                                                              // Initialize the buffer
  getnameinfo((struct sockaddr *)&ctx->addr, ctx->addrlen, addrstr, sizeof(addrstr), NULL, 0, NI_NUMERICHOST); // Get the client's IP address
  printf("[%s] \"%s %s %s\"\n", addrstr, method, path, version);                                               // Print the request line to the console

  // Only support GET and HEAD
  int is_head = 0;                                                                          // Initialize a flag for the HEAD method
  if (!strcmp(method, "GET"))                                                               // If the method is GET
    is_head = 0;                                                                            // do nothing
  else if (!strcmp(method, "HEAD"))                                                         // If the method is HEAD
    is_head = 1;                                                                            // set the flag
  else                                                                                      // For any other method
  {                                                                                         // Start of else block
    send_error(ctx->client, 405, "Method Not Allowed", "Only GET and HEAD are supported."); // send a 405 error
    return;                                                                                 // Close the connection
  } // End of else block

  // Require path starts with '/'
  if (path[0] != '/')                                                     // If the path does not start with a slash
  {                                                                       // Start of if block
    send_error(ctx->client, 400, "Bad Request", "Invalid request path."); // it's a bad request
    return;                                                               // Close the connection
  } // End of if block

  // Map URL to filesystem path
  char fs_path[PATH_MAX];                                                               // Declare a buffer for the filesystem path
  int map_rc = map_url_to_fs(ctx->cfg, path, fs_path, sizeof(fs_path));                 // Map the URL path to a filesystem path
  if (map_rc == -2)                                                                     // If the file is not found
  {                                                                                     // Start of if block
    send_error(ctx->client, 404, "Not Found", "The requested resource was not found."); // send a 404 error
    return;                                                                             // Close the connection
  } // End of if block
  if (map_rc != 0)                                               // If there's any other mapping error (like forbidden)
  {                                                              // Start of if block
    send_error(ctx->client, 403, "Forbidden", "Access denied."); // send a 403 error
    return;                                                      // Close the connection
  } // End of if block

  // If it's a directory: try index.html; else, generate listing
  int isdir = 0;                                            // Initialize a flag to indicate if the path is a directory
  if (path_stat_isdir(fs_path, &isdir, NULL) == 0 && isdir) // Check if the path is a directory
  {                                                         // Start of if block
    char idx[PATH_MAX];                                     // Declare a buffer for the index.html path
    snprintf(idx, sizeof(idx), "%s/index.html", fs_path);   // Construct the path to index.html in that directory
    if (path_stat_isdir(idx, NULL, NULL) == 0)              // Check if index.html exists and is a file
    {                                                       // Start of if block
      // It's a file; serve it
      send_file(ctx->client, idx, is_head); // Serve the index.html file
    } // End of if block
    else // If index.html does not exist
    {    // Start of else block
      // Directory listing
      send_dir_listing(ctx->client, path, fs_path); // generate and send a directory listing
    } // End of else block
    return; // Close the connection
  } // End of if block

  // Serve as file
  send_file(ctx->client, fs_path, is_head); // If the path is a file, serve it
} // End of handle_client function body

/* Thread entry point wrapper. Detaches/cleans up after serving the client */
static void *client_thread(void *arg)      // Defines the entry point for a new client thread
{                                          // Start of client_thread function body
  client_ctx_t *ctx = (client_ctx_t *)arg; // Cast the argument to a client context pointer
  handle_client(ctx);                      // Handle the client connection
  CLOSESOCK(ctx->client);                  // Close the client socket
  free(ctx);                               // Free the client context structure
  return NULL;                             // Return NULL as the thread result
} // End of client_thread function body

/* Parse a simple key=value config file. Updates cfg for keys 'root' and 'port' */
static int parse_config_file(const char *path, server_config_t *cfg) // Defines a function to parse a configuration file
{                                                                    // Start of parse_config_file function body
  FILE *f = fopen(path, "r");                                        // Open the configuration file for reading
  if (!f)                                                            // If opening fails
    return -1;                                                       // return an error
  char line[1024];                                                   // Declare a buffer to read lines from the file
  while (fgets(line, sizeof(line), f))                               // Loop through each line in the file
  {                                                                  // Start of while loop body
    char *s = strtrim(line);                                         // Trim whitespace from the line
    if (*s == '#' || *s == '\0')                                     // If the line is a comment or empty
      continue;                                                      // skip it
    char *eq = strchr(s, '=');                                       // Find the equals sign
    if (!eq)                                                         // If not found
      continue;                                                      // skip the line
    *eq = '\0';                                                      // Null-terminate the key
    char *key = strtrim(s);                                          // Trim whitespace from the key
    char *val = strtrim(eq + 1);                                     // Trim whitespace from the value
    if (strcasecmp(key, "root") == 0)                                // If the key is "root"
    {                                                                // Start of if block
      strncpy(cfg->root, val, sizeof(cfg->root) - 1);                // copy the value to the root configuration
      cfg->root[sizeof(cfg->root) - 1] = 0;                          // Ensure it's null-terminated
    } // End of if block
    else if (strcasecmp(key, "port") == 0) // If the key is "port"
    {                                      // Start of else if block
      cfg->port = atoi(val);               // convert the value to an integer and set the port configuration
    } // End of else if block
  } // End of while loop body
  fclose(f); // Close the configuration file
  return 0;  // Return 0 to indicate success
} // End of parse_config_file function body

/* Parse command line. Recognized options:
   -r <root>    Document root
   -p <port>    TCP port
   -c <file>    Config file with 'root=' and 'port='
*/
static int parse_args(int argc, char **argv, server_config_t *cfg, char *cfgfile, size_t cfgfile_sz) // Defines a function to parse command-line arguments
{                                                                                                    // Start of parse_args function body
  cfgfile[0] = 0;                                                                                    // Initialize the config file path to an empty string
  for (int i = 1; i < argc; i++)                                                                     // Loop through each command-line argument
  {                                                                                                  // Start of for loop body
    if (!strcmp(argv[i], "-r") && i + 1 < argc)                                                      // If the argument is "-r" and there is a value
    {                                                                                                // Start of if block
      strncpy(cfg->root, argv[++i], sizeof(cfg->root) - 1);                                          // set the document root
      cfg->root[sizeof(cfg->root) - 1] = 0;                                                          // Ensure it's null-terminated
    } // End of if block
    else if (!strcmp(argv[i], "-p") && i + 1 < argc) // If the argument is "-p" and there is a value
    {                                                // Start of else if block
      cfg->port = atoi(argv[++i]);                   // set the port
    } // End of else if block
    else if (!strcmp(argv[i], "-c") && i + 1 < argc) // If the argument is "-c" and there is a value
    {                                                // Start of else if block
      strncpy(cfgfile, argv[++i], cfgfile_sz - 1);   // set the config file path
      cfgfile[cfgfile_sz - 1] = 0;                   // Ensure it's null-terminated
    } // End of else if block
    else                                                              // For any other argument
    {                                                                 // Start of else block
      fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]); // print an error
      return -1;                                                      // Return an error
    } // End of else block
  } // End of for loop body
  return 0; // Return 0 to indicate success
} // End of parse_args function body

/* Create, bind, and listen on a TCP socket for the specified port on all interfaces
   Returns the listening socket or INVALID_SOCKET on error */
static sock_t create_listen_socket(int port) // Defines a function to create and prepare a listening socket
{                                            // Start of create_listen_socket function body
  sock_t s = INVALID_SOCKET;                 // Initialize the socket descriptor to an invalid value

  // Prepare hints for getaddrinfo to support both IPv4 and IPv6
  char portstr[16];                               // Declare a buffer for the port string
  snprintf(portstr, sizeof(portstr), "%d", port); // Convert the port number to a string

  struct addrinfo hints, *res = NULL, *rp = NULL; // Declare address info structures
  memset(&hints, 0, sizeof(hints));               // Zero out the hints structure
  hints.ai_family = AF_UNSPEC;                    // Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;                // Specify a TCP socket
  hints.ai_flags = AI_PASSIVE;                    // For binding to a local address
  hints.ai_protocol = IPPROTO_TCP;                // Specify the TCP protocol

  int gai = getaddrinfo(NULL, portstr, &hints, &res);        // Get a list of address structures
  if (gai != 0)                                              // If getaddrinfo fails
  {                                                          // Start of if block
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai)); // print an error
    return INVALID_SOCKET;                                   // Return an invalid socket
  } // End of if block

  for (rp = res; rp != NULL; rp = rp->ai_next)                           // Loop through the list of address structures
  {                                                                      // Start of for loop body
    s = (sock_t)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol); // Create a socket
    if (s == INVALID_SOCKET)                                             // If socket creation fails
      continue;                                                          // try the next address

    int opt = 1;                                                        // Set an option value to 1
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)); // Allow reuse of the local address
#ifdef SO_REUSEPORT                                                     // If SO_REUSEPORT is defined
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)); // allow reuse of the port
#endif                                                                  // End of SO_REUSEPORT block

    if (bind(s, rp->ai_addr, (int)rp->ai_addrlen) == 0) // Bind the socket to the address
    {                                                   // Start of if block
      if (listen(s, 128) == 0)                          // Start listening for connections
      {                                                 // Start of if block
        break;                                          // If successful, exit the loop
      } // End of if block
    } // End of if block
    CLOSESOCK(s);       // If binding or listening fails, close the socket
    s = INVALID_SOCKET; // Reset the socket descriptor to invalid
  } // End of for loop body
  freeaddrinfo(res); // Free the list of address structures
  return s;          // Return the listening socket
} // End of create_listen_socket function body

/* Print usage information */
static void print_usage(const char *prog) // Defines a function to print usage information
{                                         // Start of print_usage function body
  fprintf(stderr,                         // Print to standard error
          "Usage:\n"
          "  %s -r <root_dir> -p <port>\n"
          "  %s -c <config_file>\n"
          "Options:\n"
          "  -r   Document root directory to serve\n"
          "  -p   TCP port to listen on (e.g., 8080)\n"
          "  -c   Config file with lines: root=..., port=...\n",
          prog, prog);
} // End of print_usage function body

int main(int argc, char **argv)              // The main entry point of the program
{                                            // Start of main function body
#ifdef _WIN32                                // If compiling on Windows
  WSADATA wsa;                               // declare a WSADATA structure
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) // initialize Winsock
  {                                          // Start of if block
    fprintf(stderr, "WSAStartup failed\n");  // If initialization fails, print an error
    return 1;                                // Exit with an error code
  } // End of if block
#else  // If not compiling on Windows
  signal(SIGPIPE, SIG_IGN); // ignore the SIGPIPE signal to prevent crashing when a client disconnects
#endif // End of platform-specific block

  server_config_t cfg;          // Declare a server configuration structure
  memset(&cfg, 0, sizeof(cfg)); // Zero out the configuration structure
  // Defaults
  cfg.port = 8080;                     // Set the default port
#ifdef _WIN32                          // If compiling on Windows
  _getcwd(cfg.root, sizeof(cfg.root)); // get the current working directory
#else                                  // If not compiling on Windows
  getcwd(cfg.root, sizeof(cfg.root)); // get the current working directory
#endif                                 // End of platform-specific block

  char cfgfile[PATH_MAX];                                          // Declare a buffer for the config file path
  if (parse_args(argc, argv, &cfg, cfgfile, sizeof(cfgfile)) != 0) // Parse command-line arguments
  {                                                                // Start of if block
    print_usage(argv[0]);                                          // If parsing fails, print usage information
    return 1;                                                      // Exit with an error code
  } // End of if block

  if (cfgfile[0])                                                   // If a config file was specified
  {                                                                 // Start of if block
    if (parse_config_file(cfgfile, &cfg) != 0)                      // parse the config file
    {                                                               // Start of if block
      fprintf(stderr, "Failed to read config file: %s\n", cfgfile); // If parsing fails, print an error
      return 1;                                                     // Exit with an error code
    } // End of if block
  } // End of if block

  if (cfg.root[0] == '\0' || cfg.port <= 0 || cfg.port > 65535) // Validate the configuration
  {                                                             // Start of if block
    print_usage(argv[0]);                                       // If invalid, print usage information
    return 1;                                                   // Exit with an error code
  } // End of if block

  // Canonicalize and store root_real for security checks
  if (canonicalize_path(cfg.root, cfg.root_real, sizeof(cfg.root_real)) != 0) // Get the canonical path of the document root
  {                                                                           // Start of if block
    fprintf(stderr, "Invalid document root: %s\n", cfg.root);                 // If it fails, print an error
    return 1;                                                                 // Exit with an error code
  } // End of if block
#ifdef _WIN32 // If compiling on Windows
  // Normalize cfg.root to canonical form too (for consistent joins)
  strncpy(cfg.root, cfg.root_real, sizeof(cfg.root) - 1); // copy the canonical path back to the root config
  cfg.root[sizeof(cfg.root) - 1] = 0;                     // Ensure it's null-terminated
#endif                                                    // End of platform-specific block

  // Show config
  printf("Serving root: %s\n", cfg.root_real); // Print the serving root
  printf("Listening on port: %d\n", cfg.port); // Print the listening port

  sock_t ls = create_listen_socket(cfg.port);                                    // Create the listening socket
  if (ls == INVALID_SOCKET)                                                      // If creation fails
  {                                                                              // Start of if block
    fprintf(stderr, "Failed to create listening socket on port %d\n", cfg.port); // print an error
#ifdef _WIN32                                                                    // If compiling on Windows
    WSACleanup();                                                                // clean up Winsock
#endif                                                                           // End of platform-specific block
    return 1;                                                                    // Exit with an error code
  } // End of if block

  for (;;)                                                               // Loop indefinitely to accept client connections
  {                                                                      // Start of for loop body
    client_ctx_t *ctx = (client_ctx_t *)calloc(1, sizeof(client_ctx_t)); // Allocate memory for a new client context
    if (!ctx)                                                            // If allocation fails
    {                                                                    // Start of if block
      fprintf(stderr, "Out of memory\n");                                // print an error
      break;                                                             // Exit the loop
    } // End of if block

    ctx->addrlen = sizeof(ctx->addr);                                       // Set the address length
    ctx->client = accept(ls, (struct sockaddr *)&ctx->addr, &ctx->addrlen); // Accept a new client connection
    if (ctx->client == INVALID_SOCKET)                                      // If accept fails
    {                                                                       // Start of if block
      free(ctx);                                                            // free the context
      continue;                                                             // Continue to the next iteration
    } // End of if block
    ctx->cfg = &cfg; // Set the configuration pointer in the context

    // Spawn thread to handle client
#ifdef _WIN32                                                            // If compiling on Windows
    uintptr_t th = _beginthreadex(NULL, 0, client_thread, ctx, 0, NULL); // create a new thread
    if (th == 0)                                                         // If thread creation fails
    {                                                                    // Start of if block
      fprintf(stderr, "Failed to create thread\n");                      // print an error
      CLOSESOCK(ctx->client);                                            // Close the client socket
      free(ctx);                                                         // Free the context
      continue;                                                          // Continue to the next iteration
    } // End of if block
    CloseHandle((HANDLE)th); // Detach the thread
#else                        // If not compiling on Windows
    pthread_t tid;                                           // declare a thread ID
    if (pthread_create(&tid, NULL, client_thread, ctx) != 0) // create a new thread
    {                                                        // Start of if block
      fprintf(stderr, "Failed to create thread\n");          // If thread creation fails, print an error
      CLOSESOCK(ctx->client);                                // Close the client socket
      free(ctx);                                             // Free the context
      continue;                                              // Continue to the next iteration
    } // End of if block
    pthread_detach(tid); // Detach the thread
#endif                       // End of platform-specific block
  } // End of for loop body

  CLOSESOCK(ls); // Close the listening socket
#ifdef _WIN32    // If compiling on Windows
  WSACleanup();  // clean up Winsock
#endif           // End of platform-specific block
  return 0;      // Exit successfully
} // End of main function body
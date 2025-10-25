import socket
import threading
import argparse
import os
import csv
import mimetypes
from datetime import datetime
from wsgiref.handlers import format_date_time

class WebServer:
    def __init__(self, port, root):
        self.port = port
        self.root = os.path.abspath(root)
        self.socket = None
        self._server_name = "NicksDiscountServer/1.0"

        if not os.path.isdir(self.root):
            print(f"Error: Root folder: '{self.root}' - is not a directory.")
            exit(1)

    def start(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM) 
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind(('', self.port))
            self.socket.listen(5)
            print(f"Serving HTTP on port: {self.port} from root folder '{self.root}'!!!")

            while True:
                clientsocket, addr = self.socket.accept()
                print(f"Connection from {addr} accepted and running...")
                client_thread = threading.Thread(target=self.handle_client, args=(clientsocket,))
                client_thread.daemon = True
                client_thread.start()

        except KeyboardInterrupt:
            print("\nShutting down server.")
        except Exception as e:
            print(f"Server error: {e}")
        finally:
            if self.socket:
                self.socket.close()

    def handle_client(self, clientsocket):
        try:
            request_data = clientsocket.recv(1024).decode('utf-8')
            if not request_data:
                return

            request_line = request_data.splitlines()[0]
            method, path, blank = request_line.split()

            if method != 'GET':
                self.send_error(clientsocket, 404, "File Not Found")
                return

            if path.endswith('/'):
                path += 'index.html'
            
            requested_path = os.path.abspath(os.path.join(self.root, path.lstrip('/')))
            if not requested_path.startswith(self.root):
                self.send_error(clientsocket, 404, "File Not Found")
                return

            if os.path.exists(requested_path) and os.path.isfile(requested_path):
                self.send_file(clientsocket, requested_path)
            else:
                self.send_error(clientsocket, 404, "File Not Found")

        except Exception as e:
            print(f"Error found in client (handle_client): {e}")
        finally:
            clientsocket.close()

    def send_file(self, clientsocket, filepath):
        try:
            with open(filepath, 'rb') as f:
                content = f.read()
            
            content_length = len(content)
            content_type = mimetypes.guess_type(filepath)[0] or 'application/octet-stream'
            
            response_headers = self.build_headers(200, "OKEE DOKEE", content_type, content_length)
            clientsocket.sendall(response_headers.encode('utf-8'))
            clientsocket.sendall(content)

        except Exception as e:
            print(f"Error sending file: {e}")
            self.send_error(clientsocket, 404, "File Not Found")

    def send_error(self, clientsocket, status_code, status_message):
        body = f"<html><body><h1>{status_code} {status_message}</h1></body></html>".encode('utf-8')
        content_length = len(body)
        content_type = 'text/html'
        
        response_headers = self.build_headers(status_code, status_message, content_type, content_length)
        clientsocket.sendall(response_headers.encode('utf-8'))
        clientsocket.sendall(body)

    def build_headers(self, status_code, status_message, content_type, content_length):
        headers = []
        headers.append(f"HTTP/1.1 {status_code} {status_message}")
        
        now = datetime.now()
        stamp = format_date_time(now.timestamp())
        headers.append(f"Date: {stamp}")
        
        headers.append(f"Server: {self._server_name}")
        headers.append(f"Content-Type: {content_type}")
        headers.append(f"Content-Length: {content_length}")
        headers.append("Connection: close")
        headers.append("\r\n")
        
        return "\r\n".join(headers)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Simple Python HTTP Server")
    parser.add_argument('-p', '--port', type=int, default=8080, help="Port to listen on")
    parser.add_argument('-r', '--root', type=str, default='./www', help="Document root directory")
    args = parser.parse_args()

    mimetypes.add_type("text/css", ".css")
    mimetypes.add_type("image/jpeg", ".jpg")
    mimetypes.add_type("image/jpeg", ".jpeg")
    mimetypes.add_type("image/png", ".png")
    mimetypes.add_type("image/gif", ".gif")

    server = WebServer(args.port, args.root) 
    server.start()

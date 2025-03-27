import json
import time
import os
import errno
from src.deepseek_analizer import *

class TarantoolClusterClient:
    def __init__(self):
        self.fifo_in = "/tmp/fifo/server_in.fifo"
        self.fifo_out = "/tmp/fifo/server_out.fifo"
        self._ensure_fifos_exist()
        
    def _ensure_fifos_exist(self):
        os.makedirs("/tmp/fifo", exist_ok=True)
        for fifo in [self.fifo_in, self.fifo_out]:
            if not os.path.exists(fifo):
                os.mkfifo(fifo)
                
    def _write_request(self, request):
        """Atomic write to input FIFO"""
        with open(self.fifo_in, 'w') as f:
            f.write(json.dumps(request) + '\n')
            
    def _read_response(self, timeout=10):
        """Read response with timeout handling"""
        start = time.time()
        while time.time() - start < timeout:
            try:
                with open(self.fifo_out, 'r') as f:
                    response = f.read().strip()
                    if response:
                        return json.loads(response)
            except (IOError, OSError) as e:
                if e.errno != errno.EAGAIN:
                    raise
            time.sleep(0.1)
        raise TimeoutError("Server response timeout")

    def send_start(self, count):
        request = {"path": "/start", "data": {"count": count}}
        try:
            self._write_request(request)
            return self._read_response()
        except Exception as e:
            print(f"Start request failed: {str(e)}")
            return None

    def send_operations(self, operations):
        request = {"path": "/simulate", "data": {"operations": operations["operations"]}}
        try:
            self._write_request(request)
            return self._read_response()
        except Exception as e:
            print(f"Operations request failed: {str(e)}")
            return None

    def wait_for_start(self, count, max_retries=30, delay=1):
        for _ in range(max_retries):
            response = self.send_start(count)
            if response and response.get('body', {}).get('created') == 1:
                print("Cluster started successfully")
                return True
            print(f"Waiting for cluster start ({_+1}/{max_retries})...")
            time.sleep(delay)
        print("Cluster startup failed")
        return False

    def process(self, client_response):
        try:
            analysis = analyzer.analyze_feedback(client_response)
            message = analysis["messages"][-1].content
            try:
                return json.dumps(json.loads(message)), None
            except json.JSONDecodeError:
                return f"Raw response: {message}", None
        except Exception as e:
            return None, str(e)

    def work(self, count):
        if not self.wait_for_start(count):
            return

        current_state = json.dumps({
            "nodes_count": count,
            "logs": "",
            "has_error": 0
        })

        while True:
            try:
                processed, error = self.process(current_state)
                if error:
                    print(f"Processing error: {error}")
                    break
                if not processed:
                    print("No operations generated")
                    break

                operations = json.loads(processed)
                print("Sending operations:", operations)
                
                response = self.send_operations(operations)
                if not response:
                    print("No response from server")
                    break
                
                current_state = json.dumps(response['body'])
                print("New state:", current_state)
                
            except json.JSONDecodeError as e:
                print(f"JSON Error: {str(e)}")
                break
            except KeyboardInterrupt:
                print("Exiting by user request")
                break
            except Exception as e:
                print(f"Unexpected error: {str(e)}")
                break

# if __name__ == "__main__":
#     client = TarantoolClusterClient()
#     client.work(3)
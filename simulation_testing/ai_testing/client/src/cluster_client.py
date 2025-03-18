import requests
import json
import time
from src.deepseek_analizer import *  # Ensure correct import


class TarantoolClusterClient:
    def __init__(self, url):
        self.base_url = url
        self.session = requests.Session()
        self.session.headers.update({'Content-Type': 'application/json'})

    def send_start(self, count):
        """Отправляет запрос на запуск кластера"""
        payload = {"count": count}
        print(payload)
        try:
            print(f"Sending start request with payload: {json.dumps(payload)}")  # Log payload
            response = self.session.post(
                f"{self.base_url}/start",
                json=payload,
            )

            # Print the request headers and payload
            print("Request Headers:", response.request.headers)  # Debug print
            print("Request Body:", response.request.body)      # Debug print
            
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Request failed: {str(e)}")
            return None

    def send_operations(self, operations):
        """Отправляет операции на сервер и возвращает результат"""
        payload =  operations
        
        try:
            print(f"Sending operations request with payload: {json.dumps(payload)}")  # Log payload
            response = self.session.post(
                f"{self.base_url}/simulate",
                json=payload
            )
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Request failed: {str(e)}")
            return None

    def wait_for_start(self, count, max_retries=100, delay=5):
        """Ожидает успешного запуска кластера"""
        for _ in range(max_retries):
            response = self.send_start(count)
            if response and response.get('created') == 1:
                print("Cluster started successfully!")
                return True
            print("Waiting for cluster to start...")
            time.sleep(delay)
        print("Cluster failed to start after retries.")
        return False

    def process(self, client_response):
        """Processes server response using analyzer"""
        try:
            request = analyzer.analyze_feedback(client_response)
            message_content = request["messages"][-1].content
            try:
                output = json.loads(message_content)
                return json.dumps(output, indent=2, ensure_ascii=False)
            except json.JSONDecodeError:
                return f"Raw response: {message_content}"
        except Exception as e:
            print(f"Error in processing: {str(e)}")
            return None
        
    def work(self, count):
        """Main workflow to manage cluster operations"""
        if not self.wait_for_start(count):
            print("Exiting due to cluster start failure")
            return

        # Initialize with proper JSON format
        current_state = json.dumps({
            "nodes_count": count,
            "logs": "",
            "has_error": 0
        })

        while True:
            try:
                # Process current state
                processed = self.process(current_state)
                if not processed:
                    print("No operations generated")
                    break

                # Parse operations
                operations = json.loads(processed)
                print("Generated operations:", operations)

                # Send operations and get new state
                new_state = self.send_operations(operations)
                if not new_state:
                    print("No response from operations endpoint")
                    break

                # Update current state
                current_state = json.dumps(new_state)
                print("New cluster state:", current_state)
                time.sleep(1)

            except json.JSONDecodeError as e:
                print(f"JSON decoding failed: {str(e)}")
                break
            except KeyboardInterrupt:
                print("Exiting by user request")
                break
            except Exception as e:
                print(f"Unexpected error: {str(e)}")
                break

# Пример использования
# if __name__ == "__main__":
#     client = TarantoolClusterClient("http://0.0.0.0:9090")

#     if client.wait_for_start(3):
#         current_state = json.dumps({
#             "nodes_count": 3,
#             "logs": "",
#             "has_error": 0
#         })
#         while True:
#             try:
#                 request = analyzer.analyze_feedback(current_state)
                                
#             except Exception as e:
#                 print(f"Error in analyzer: {str(e)}")
        # operations = [
        #     {"crash_type": 0, "node_1": 1, "node_2": -1, "crash_time": 5},
        #     {"crash_type": 1, "node_1": 2, "node_2": -1, "crash_time": 7},
        #     {"crash_type": 2, "node_1": 1, "node_2": 3, "crash_time": 10}
        # ]
        # print("Sending operations to cluster...")
        # response = client.send_operations(operations)
        
        # if response and response.get('has_error') == 0:
        #     print("\nOperations are being processed asynchronously...")
        #     for i in range(1, 12):
        #         print(f"Time elapsed: {i} seconds")
        #         time.sleep(1)
        # print(response)
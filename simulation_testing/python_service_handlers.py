import requests
import json
import time

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
        payload = {"operations": operations}
        
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

# Пример использования
if __name__ == "__main__":
    client = TarantoolClusterClient("http://0.0.0.0:9090")

    if client.wait_for_start(3):
        operations = [
            {"crash_type": 0, "node_1": 1, "node_2": -1, "crash_time": 5},
            {"crash_type": 1, "node_1": 2, "node_2": -1, "crash_time": 7},
            {"crash_type": 2, "node_1": 1, "node_2": 3, "crash_time": 10}
        ]
        print("Sending operations to cluster...")
        response = client.send_operations(operations)
        
        if response and response.get('has_error') == 0:
            print("\nOperations are being processed asynchronously...")
            for i in range(1, 12):
                print(f"Time elapsed: {i} seconds")
                time.sleep(1)
        print(response)

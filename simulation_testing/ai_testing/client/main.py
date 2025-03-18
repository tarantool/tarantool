from src.cluster_client import *
if __name__ == "__main__":
    client = TarantoolClusterClient("http://0.0.0.0:9090")
    client.work(3)
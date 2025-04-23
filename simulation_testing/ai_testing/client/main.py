from src.fifo_client import *

if __name__ == "__main__":
    client = TarantoolClusterClient()
    client.work(5)

from wit_world import exports
from os import getenv

class Run(exports.Run):
    def run(self) -> None:
        name = getenv("GREETING", "unknown")
        print(f"Hello, {name}! From run.")

class WitWorld:
    def say_hello(self, name: str) -> str:
        return f"Hello, {name}! From exported function."

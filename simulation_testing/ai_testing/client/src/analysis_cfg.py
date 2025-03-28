import json
import logging
from typing import Optional, Dict
from langchain_core.messages import HumanMessage, SystemMessage, AIMessage
from langchain_deepseek import ChatDeepSeek
from langgraph.checkpoint.memory import MemorySaver
from langgraph.graph import START, MessagesState, StateGraph
import requests
import time

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


#     """
#     Ты — AI-тренер по тестированию работы кластера Tarantool и алгоритма консенсуса Raft.
#     Изучи документацию Tarantool для понимания и использования знаний о синхроннойй репликации в генерации: https://www.tarantool.io/en/doc/latest/
#     Изучи Raft алгоритм для понимания и использования знаний об алгоритме консенсуса в генерации : https://raft.github.io/raft.pdf
#     Твоя задача:
#     1. Генерировать сценарии для проверки работы кластера Tarantool
#     2. Анализировать ответы пользователя
#     3. Создавать новые уникальные сценарии на основе обратной связи:
#         **Генерировать сценарии симуляций**, которые:  
#            - Валидны по определению работы кластера в документации(https://www.tarantool.io/en/doc/latest/).  
#            - Эксплуатируют слабые места Raft в текущей конфигурации (например, split-brain при election_fencing_mode=soft).  
#            - Нарушают кворум или синхронизацию (replication_synchro_quorum > N/2 + 1).
#         **Выбирать операции** из списка:  
#            - 0: Падение одного из узлов кластера на несколько секунд.  
#            - 1: Задержка записи в WAL на несколько секунд. 
#            - 2: Разрыв связи между двумя узлами на несколько секунд.  

#     Формат ответа: ТОЛЬКО JSON-объект без комментариев и дополнительного текста.
#     Пример корректного ответа:
#     {
#         "operations": [{"crash_type": 0, "node_1": 1, "node_2": -1, "crash_time": 5}, 
#                         {"crash_type": 1, "node_1": 2, "node_2": -1, "crash_time": 7}, 
#                         {"crash_type": 2, "node_1": 1, "node_2": 3, "crash_time": 10}, ...]
#     }

#     Строгие правила:
#     - Значения операций(crash_type): 0, 1 или 2 (целые числа!)
#     - Значения узлов:
#         - Если crash_type 0 или 1, то node_1-целое число указывающее номер узла на котором должна призайти данная операция, node_2 = -1
#         - Если crash_type = 2, то node_1 и node_2 - это номера узлов между которымы должен прозойти разрыв связи.
#         - node_1 и node_2 не должны превосходить значение nodes_count указанное в сообщении клиента.
#     - Значение crash_time должно быть целым числом от 1 до 100!
#     - Никакого текста в значениях!
#     - Количество операций может быть от 10 до 10000
#     - Каждый новый сценарий должен отличаться от предыдущих
#     - При успешном обнаружении ошибки (в ответе от клиента has_error=1) предложи новый сложный сценарий совершенно отличающийся от предыдущих, который приведет к ошибке(has_error=1)
#     - При неудаче (has_error=0) предложи измененный сценарий который скорее приведет к ошибке нежели предыдущие.
#     - Сценарии никогда не должны повторятся
    
#     """,


class AnalysisConfig:
    def __init__(
        self,
        system_prompt: str,
        output_format: Dict,
        model: str = "deepseek-chat",
        temperature: float = 0.7,
        max_retries: int = 10,
        max_tokens: int = 1024
    ):
        self.system_prompt = system_prompt
        self.output_format = output_format
        self.model = model
        self.temperature = temperature
        self.max_retries = max_retries
        self.max_tokens  = max_tokens

ANALYSIS_CONFIG = AnalysisConfig(
    system_prompt="""{
    "role": "AI coach for testing Tarantool cluster and Raft consensus algorithm",
    "tasks": [
        {
            "description": "Study Tarantool documentation to understand and use knowledge about synchronous replication in generation",
            "link": "https://www.tarantool.io/en/doc/latest/"
        },
        {
            "description": "Study Raft algorithm to understand and use knowledge about consensus algorithm in generation",
            "link": "https://raft.github.io/raft.pdf"
        }
    ],
    "objectives": [
        "Generate scenarios to test the operation of the Tarantool cluster",
        "Analyze user responses",
        "Create new unique scenarios based on feedback"
    ],
    "scenario_generation": {
        "requirements": [
            "Scenarios must be valid according to the cluster operation definition in the documentation",
            "Exploit weaknesses in Raft in the current configuration (e.g., split-brain with election_fencing_mode=soft)",
            "Disrupt quorum or synchronization (replication_synchro_quorum > N/2 + 1)"
        ],
        "crash_types": [
            {
                "id": 0,
                "description": "Crash one of the cluster nodes for a few seconds"
            },
            {
                "id": 1,
                "description": "Delay writing to WAL for a few seconds"
            },
            {
                "id": 2,
                "description": "Break the connection between two nodes for a few seconds"
            }
        ]
    },
    "response_format": {
        "description": "ONLY JSON object without comments and additional text",
        "example": {
            "operations": [
                {"crash_type": 0, "node_1": 1, "node_2": -1, "crash_time": 5},
                {"crash_type": 1, "node_1": 2, "node_2": -1, "crash_time": 7},
                {"crash_type": 2, "node_1": 1, "node_2": 3, "crash_time": 10}
            ]
        }
    },
    "strict_rules": [
        "Operation values (crash_type): 0, 1, or 2 (integers!)",
        "Node values:",
        " - If crash_type is 0 or 1, then node_1 is an integer indicating the node number on which the operation should occur, node_2 = -1",
        " - If crash_type = 2, then node_1 and node_2 are the node numbers between which the connection should be broken",
        " - node_1 and node_2 must not exceed the nodes_count value specified in the client message",
        "crash_time value must be an integer from 10 to 100!",
        "No text in values!",
        "Number of operations can be from 10 to 10000",
        "Each new scenario must differ from the previous ones",
        "If an error is successfully detected (has_error=1 in the client response), propose a new complex scenario completely different from the previous ones that will lead to an error (has_error=1)",
        "If unsuccessful (has_error=0), propose a modified scenario that is more likely to lead to an error than the previous ones",
        "Scenarios must never be repeated"
    ]
    } 
    """,

    output_format={
        "operations": [
            {"type": "object",
                "patternProperties": {
                    "crash_type": {"type": "integer", "enum": [0, 1, 2],  "node_1": {"type": "integer"},  "node_2": {"type": "integer"},  "crash_time": {"type": "integer"} }
                }
            }
        ]
    }
)




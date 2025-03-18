from src.analysis_cfg import *

class DeepSeekAnalyzer:
    def __init__(self, api_key: str, config: AnalysisConfig):
        self.llm = ChatDeepSeek(
            model=config.model,
            temperature=config.temperature,
            max_retries=config.max_retries,
            max_tokens=config.max_tokens,
            api_key=api_key
        )
        self.config = config
        self.workflow = self._build_workflow()
        
    def _build_workflow(self):
        workflow = StateGraph(state_schema=MessagesState)
        workflow.add_node("analyze_feedback", self._analyze_feedback)
        workflow.add_edge(START, "analyze_feedback")
        workflow.set_entry_point("analyze_feedback")
        return workflow.compile(checkpointer=MemorySaver())
        

    def _clean_json_response(self, response: str) -> str:
        response = response.replace('```json', '').replace('```', '').strip()
        if response.startswith('{') and response.endswith('}'):
            return response
        if '{' in response and '}' in response:
            start = response.find('{')
            end = response.rfind('}') + 1
            return response[start:end]
        return response

    def _analyze_feedback(self, state: MessagesState):
        system_message = SystemMessage(content=self.config.system_prompt)
        user_message = state["messages"][-1]
        
        print(f"Processing log: {user_message.content[:100]}...")
        
        try:
            response = self.llm.invoke([
                system_message,
                HumanMessage(content=f"{user_message.content}\n\nОтвет ТОЛЬКО в JSON:")
            ])
            
            raw_content = response.content
            print(f"Raw model response: {raw_content}")
            
            cleaned_content = self._clean_json_response(raw_content)
            data = json.loads(cleaned_content)
            
            return {"messages": [AIMessage(content=json.dumps(data))]}
            
        except Exception as e:
            print(f"Analysis failed: {str(e)}")
            return {"messages": [AIMessage(content=json.dumps({
                "error": str(e),
                "details": "Ошибка при обработке ответа модели"
            }))]}

    def analyze_feedback(self, logs: str, thread_id: str = "default") -> Dict:
        config = {"configurable": {"thread_id": thread_id}}
        return self.workflow.invoke(
            {"messages": [HumanMessage(content=logs)]},
            config
        )
    
    def start(self, thread_id: str = "default") -> Dict:
        config = {"configurable": {"thread_id": thread_id}}
        return self.workflow.invoke(
            {"messages": [HumanMessage(content="0")]},
            config
        )


API_KEY = "sk-e123e150c99e4f14b47f813315838974"
    
analyzer = DeepSeekAnalyzer(
    api_key=API_KEY,
    config=ANALYSIS_CONFIG
)
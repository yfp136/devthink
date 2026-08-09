// 模型服务商预设（均兼容 OpenAI /chat/completions 协议）。
// 选中预设会自动填入接口地址与推荐模型；API Key 与精确模型名仍需你填写。
// 豆包/火山方舟的模型名有两种填法：
//   1) 直接填模型 ID，如 doubao-seed-2-0-lite-260428（控制台里开通后直接调用）
//   2) 填推理端点 ID，如 ep-xxxx（先在控制台创建端点，适合生产限流/监控）
export const PROVIDERS = [
  {
    id: 'deepseek',
    name: 'DeepSeek（深度求索）',
    baseUrl: 'https://api.deepseek.com/v1',
    models: ['deepseek-chat', 'deepseek-reasoner'],
    note: 'deepseek-reasoner 为原生思考模型，会自动展示思考链（像原生「深度思考」）。'
  },
  {
    id: 'doubao',
    name: '豆包（火山方舟 Doubao）',
    baseUrl: 'https://ark.cn-beijing.volces.com/api/v3',
    models: ['doubao-seed-2-0-lite-260428', 'doubao-seed-1.6', 'doubao-pro-32k', 'doubao-lite-32k', 'doubao-thinking-250415'],
    note: '火山方舟：模型名可填控制台开通的模型 ID（如 doubao-seed-2-0-lite-260428），也可填创建的推理端点 ID（ep-xxxx）。API Key 用 ARK_API_KEY。'
  },
  {
    id: 'qwen',
    name: '通义千问（阿里云 DashScope）',
    baseUrl: 'https://dashscope.aliyuncs.com/compatible-mode/v1',
    models: ['qwen-plus', 'qwen-max', 'qwen-turbo', 'qwen-long', 'qwen3-max'],
    note: 'qwen3 系列原生返回思考链（reasoning_content）；API Key 用 DashScope 的 API-KEY。'
  },
  {
    id: 'custom',
    name: '自定义（兼容 OpenAI 协议）',
    baseUrl: '',
    models: [],
    note: '填入任意兼容 /chat/completions 的接口地址即可，例如本地 Ollama、vLLM、Azure OpenAI 等。'
  }
]

export function providerById(id) {
  return PROVIDERS.find(p => p.id === id) || PROVIDERS.find(p => p.id === 'custom')
}

// 根据已填的 baseUrl 反推服务商（用于老配置没存 provider 字段时回显下拉框）
export function providerFromBaseUrl(baseUrl) {
  const u = (baseUrl || '').replace(/\/$/, '')
  return PROVIDERS.find(p => p.baseUrl && p.baseUrl === u)?.id || 'custom'
}

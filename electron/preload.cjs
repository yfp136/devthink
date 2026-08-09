const { contextBridge, ipcRenderer } = require('electron')

// 渲染进程通过 window.api 与主进程通信；API key 始终留在主进程，不暴露给页面。
const api = {
  platform: 'electron',
  config: {
    get: () => ipcRenderer.invoke('config:get'),
    set: (cfg) => ipcRenderer.invoke('config:set', cfg)
  },
  account: {
    register: (username, password) => ipcRenderer.invoke('account:register', { username, password }),
    login: (username, password) => ipcRenderer.invoke('account:login', { username, password }),
    logout: () => ipcRenderer.invoke('account:logout'),
    current: () => ipcRenderer.invoke('account:current'),
    seedDeveloper: () => ipcRenderer.invoke('account:seed'),
    listAccounts: () => ipcRenderer.invoke('account:list'),
    allocate: ({ username, password }) => ipcRenderer.invoke('account:allocate', { username, password }),
    reset: ({ username, newPassword }) => ipcRenderer.invoke('account:reset', { username, newPassword })
  },
  projects: {
    list: (username) => ipcRenderer.invoke('projects:list', username),
    save: (username, project) => ipcRenderer.invoke('projects:save', { username, project }),
    get: (username, id) => ipcRenderer.invoke('projects:get', { username, id }),
    delete: (username, id) => ipcRenderer.invoke('projects:delete', { username, id })
  },
  ai: {
    chat: ({ messages, mode, config }) => ipcRenderer.invoke('ai:chat', { messages, mode, config })
  },
  dialog: {
    selectFile: () => ipcRenderer.invoke('dialog:selectFile')
  },
  fs: {
    writeProject: ({ dir, files }) => ipcRenderer.invoke('fs:writeProject', { dir, files }),
    writeFiles: ({ base, files }) => ipcRenderer.invoke('fs:writeFiles', { base, files })
  },
  shell: {
    run: ({ cwd, steps, env }) => ipcRenderer.invoke('shell:run', { cwd, steps, env }),
    showItem: (p) => ipcRenderer.invoke('shell:showItem', p)
  },
  ssh: {
    test: (cfg) => ipcRenderer.invoke('ssh:test', cfg),
    exec: (cfg) => ipcRenderer.invoke('ssh:exec', cfg),
    sftp: (cfg) => ipcRenderer.invoke('ssh:sftp', cfg)
  },
  preview: {
    start: (cfg) => ipcRenderer.invoke('preview:start', cfg),
    stop: () => ipcRenderer.invoke('preview:stop')
  }
}

contextBridge.exposeInMainWorld('api', api)

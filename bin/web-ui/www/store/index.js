import Vue from 'vue'

export const state = () => ({
  counter: 0,
  terminalOutput: '',
  funcs: {},
  currentFuncId: 1,
  currentFuncIRContent: '',
  currentFuncDecompiledContent: '',
  currentIRSelection: undefined,
  currentProvenanceMap: {}
})

export const mutations = {
  increment (state) {
    state.counter++
  },
  setFuncs (state, { funcs }) {
    Vue.set(state, 'funcs', funcs)
  },
  setCurrentFuncIRContent (state, { content }) {
    state.currentFuncIRContent = content
  },
  setCurrentFuncDecompiledContent (state, { content }) {
    state.currentFuncDecompiledContent = content
  },
  setCurrentFuncId (state, { id }) {
    state.currentFuncId = id
  },
  appendTerminalOutput (state, { text }) {
    state.terminalOutput += text
  },
  clearTerminalInput (state) {
    state.terminalOutput = ''
  },
  setCurrentIRSelection (state, { id }) {
    state.currentIRSelection = id
  },
  setCurrentProvenanceMap (state, { provenance }) {
    const newMap = {}
    for (const map in provenance) {
      for (const [from, to] of provenance[map]) {
        if (!from || !to) {
          continue
        }

        const fromHex = from.toString(16)
        const toHex = to.toString(16)
        if (!newMap[fromHex]) {
          newMap[fromHex] = []
        }
        newMap[fromHex].push(toHex)

        if (!newMap[toHex]) {
          newMap[toHex] = []
        }
        newMap[toHex].push(fromHex)
      }
    }
    console.log(newMap)
    Vue.set(state, 'currentProvenanceMap', newMap)
  }
}

export const actions = {
  async updateFuncs ({ commit, state, dispatch }) {
    const { output } = await this.$socket.send({
      cmd: 'lfa'
    })

    if (output.trim().length <= 0) {
      await commit('setFuncs', { funcs: {} })
      await commit('setCurrentFuncIRContent', { content: '' })
      await commit('setCurrentFuncDecompiledContent', { content: '' })
      return
    }

    const updateFuncList = {}
    const newFunctions = []
    let funcPrecedingCurr = 1
    output.trim().split('\n').forEach((l) => {
      const [idStr, name] = l.trim().split(' ')
      const id = parseInt(idStr)
      updateFuncList[id] = name
      if (!state.funcs[id]) { newFunctions.push(id) }
      if (id <= state.currentFuncId) { funcPrecedingCurr = id }
    })
    console.log(output)
    console.log(updateFuncList)

    await commit('setFuncs', { funcs: updateFuncList })

    if (newFunctions.length > 0) {
      await commit('setCurrentFuncId', { id: newFunctions[0] })
      await dispatch('updateFuncContent')
    } else if (!updateFuncList[state.currentFuncId]) {
      await commit('setCurrentFuncId', { id: funcPrecedingCurr })
      await dispatch('updateFuncContent')
    }
  },
  async updateFuncContent ({ state, commit }) {
    const { output } = await this.$socket.send({
      cmd: `dec ${state.currentFuncId}`
    })

    if (typeof output !== 'object') {
      // need better error handling
      return
    }

    const { ir, code, provenance } = output

    await commit('setCurrentFuncIRContent', {
      content: ir
    })

    await commit('setCurrentFuncDecompiledContent', {
      content: code
    })

    await commit('setCurrentProvenanceMap', {
      provenance
    })
  },
  async focusFunc ({ commit, dispatch }, { id }) {
    await commit('setCurrentFuncId', { id })
    await dispatch('updateFuncContent')
  },
  async evalCommand ({ commit, dispatch }, { cmd }) {
    console.log(cmd)
    await commit('appendTerminalOutput', { text: `> ${cmd}\n` })
    const { output } = await this.$socket.send({
      cmd
    })

    await commit('appendTerminalOutput', { text: output })
    // Update Function List UI
    await dispatch('updateFuncs')
  },
  parseWsData ({ commit, dispatch }, { cmd, output }) {
    // handle unknown data here
  },
  async uploadBitcode ({ dispatch }, { file }) {
    await this.$socket.send({
      cmd: 'upload',
      file
    })
    await dispatch('updateFuncs')
    await dispatch('updateFuncContent')
  }
}

<template>
  <div class="flex flex-col h-full w-full overflow-scroll" @click="focusInput" ref="termEl" >
    <div class="grow">
      <pre>{{ $store.state.terminalOutput }}</pre>
    </div>
    <div class="flex">
      <pre>> </pre>
      <input ref="inputEl" v-model="terminalInput" type="text" class="grow w-full terminal-input" @keyup.enter="onEnter">
    </div>
  </div>
</template>

<script>
export default {
  data () {
    return {
      terminalInput: ''
    }
  },
  updated () {
    console.log(this.$refs.termEl)
    this.$refs.termEl.scrollTo({
      left: 0,
      top: this.$refs.termEl.scrollHeight,
      behavior: 'smooth'
    })
  },
  methods: {
    focusInput () {
      console.log(document.activeElement)
      console.log(this.$refs.inputEl)
      if (document.activeElement !== this.$refs.inputEl) {
        this.$refs.inputEl.focus()
      }
    },
    onEnter () {
      if (this.terminalInput.length === 0) { return }

      const cmd = this.terminalInput
      this.terminalInput = ''

      if (cmd === 'clear') {
        this.$store.commit('clearTerminalInput')
      } else {
        this.$store.dispatch('evalCommand', { cmd })
      }
    }
  }
}
</script>

<style scoped>
.terminal-input {
  position: relative;
  background:  rgba(0 ,0,0,0);
  border: none;
  width: 1px;
  /* opacity: 0; */
  cursor: default;
}
.terminal-input:focus{
  outline: none;
  border: none;
}
</style>

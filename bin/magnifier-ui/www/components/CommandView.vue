<template>
  <div class="flex justify-evenly h-full items-center flex-wrap px-2 py-2">
    <button class="w-full rounded-full highlight-color px-2 py-1" @click="inlineFunction">
      Inline
    </button>
    <button class="w-full rounded-full highlight-color px-2 py-1" @click="optimizeFunction">
      Optimize
    </button>
    <button class="w-full rounded-full highlight-color px-2 py-1" @click="substituteValue">
      Substitute
    </button>
    <button class="w-full rounded-full highlight-color px-2 py-1" @click="deleteFunction">
      Delete
    </button>
    <button class="w-full rounded-full highlight-color px-2 py-1" @click="uploadFile">
      Upload
    </button>
  </div>
</template>

<script>
function buf2hex (buffer) {
  return [...new Uint8Array(buffer)]
    .map(x => x.toString(16).padStart(2, '0'))
    .join('')
}

export default {
  methods: {
    inlineFunction () {
      const id = this.$store.state.currentIRSelection
      if (id === undefined) { return }

      this.$store.dispatch('evalCommand', { cmd: `ic ${id}` })
    },
    optimizeFunction () {
      const id = this.$store.state.currentFuncId
      if (id === undefined) { return }

      this.$store.dispatch('evalCommand', { cmd: `o3 ${id}` })
    },
    substituteValue () {
      const id = this.$store.state.currentIRSelection
      if (id === undefined) { return }

      const val = parseInt(window.prompt('What value do you want to substitute in?', ''))
      if (isNaN(val)) { return }

      this.$store.dispatch('evalCommand', { cmd: `sv ${id} ${val}` })
    },
    deleteFunction () {
      const id = this.$store.state.currentFuncId
      if (id === undefined) { return }

      this.$store.dispatch('evalCommand', { cmd: `df! ${id}` })
    },
    uploadFile () {
      const input = document.createElement('input')
      input.type = 'file'
      input.accept = '.bc'
      input.addEventListener('change', this.onFileChange)
      input.click()
    },
    onFileChange (e) {
      console.log('hello')
      const files = e.target.files || e.dataTransfer.files
      if (!files.length) {
        return
      }

      const reader = new FileReader()
      reader.readAsArrayBuffer(files[0])

      reader.addEventListener('load', (e) => {
        console.log(reader.result)

        this.$store.dispatch('uploadBitcode', { file: buf2hex(reader.result) })
      })
    }
  }
}
</script>

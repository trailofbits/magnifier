<template>
  <div class="flex w-full flex-col justify-center h-full">
    <form id='upload-form' class="flex justify-center mb-3">
    <input
      id='file-field'
      name='file'
      type='file'
      accept=".bc"
      @change="onFileChange">
  </form>
  <div class="flex justify-center">
      <button class="rounded-full bg-sky-400 px-4 py-2" @click="uploadFile">
        Upload
      </button>
  </div>
  </div>
</template>

<script>
function buf2hex (buffer) {
  return [...new Uint8Array(buffer)]
    .map(x => x.toString(16).padStart(2, '0'))
    .join('')
}

export default {
  data () {
    return {
      files: []
    }
  },
  methods: {
    onFileChange (e) {
      const files = e.target.files || e.dataTransfer.files
      if (!files.length) {
        return
      }
      console.log(files)
      this.files = files
    },
    uploadFile () {
      if (this.files.length < 1) { return }

      const reader = new FileReader()
      reader.readAsArrayBuffer(this.files[0])

      reader.addEventListener('load', (e) => {
        console.log(reader.result)

        this.$store.dispatch('uploadBitcode', { file: buf2hex(reader.result) })
      })
    }
  }
}
</script>

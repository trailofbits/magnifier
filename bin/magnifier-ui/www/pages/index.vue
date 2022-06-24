<template>
  <Glayout
      ref="GLayoutRoot"
      glc-path="./"
      style="width: 100vw; height: 100vh"
      id="main-window"
  ></Glayout>
</template>

<script>
import Vue from 'vue'
import Glayout from '~/components/Glayout.vue'
import LayoutConfig from '~/assets/layout/config.json'

export default {
  components: { Glayout },
  async mounted () {
    await Vue.nextTick()
    // this.$refs.GLayoutRoot.addGLComponent('Terminal', 'Terminal')
    // this.$refs.GLayoutRoot.addGLComponent('FunctionList', 'Function List')
    // this.$refs.GLayoutRoot.addGLComponent('IRView', 'IR View')
    this.$refs.GLayoutRoot.loadGLLayout(LayoutConfig)
    // this.$refs.GLayoutRoot.addGLComponent('Upload', 'Upload')
    // this.$refs.GLayoutRoot.addGLComponent('CommandView', 'CommandView')

    this.$socket.init()

    await this.$store.dispatch('updateFuncs')
    await this.$store.dispatch('updateFuncContent')
    // setInterval(() => {
    //   console.log(JSON.stringify(this.$refs.GLayoutRoot.getLayoutConfig()))
    // }, 2000)
  }
}
</script>

<style>
html {
    height: 100%;
}
body {
    height: 100%;
    margin: 0;
    overflow: hidden;
}
.full-height, #app {
    height: 100%;
}
#app {
    font-family: Avenir, Helvetica, Arial, sans-serif;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
}
#nav {
    text-align: center;
}
</style>

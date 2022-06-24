<template>
  <div>
    <!-- <h1 class="main-label text-lg my-2">Functions</h1> -->
    <div v-for="(name, id) in $store.state.funcs" :key="id" :class="(id == $store.state.currentFuncId ? 'bg-emerald-200 text-black' : '')+' cursor-pointer rounded'" @click="$store.dispatch('focusFunc', {id})">
      <h1>{{ id }}: {{ name }}</h1>
    </div>
  </div>
</template>

<script>
export default {
  mounted () {
    this.$parent.$el.style.overflow = 'scroll'
  }
}
</script>

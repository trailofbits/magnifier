<template>
  <div>
    <h1 class="main-label text-lg my-2">LLVM IR (Function Id {{$store.state.currentFuncId}} - {{$store.state.funcs[$store.state.currentFuncId]}})</h1>
    <div class="overflow-y-scroll h-full">
      <pre ref="llvm" v-html="$store.state.currentFuncIRContent" />
    </div>
  </div>
</template>

<script>
export default {
  mounted () {
    this.$parent.$el.style.overflow = 'scroll'
  },
  updated () {
    for (const el of document.querySelectorAll('.selected') ?? []) {
      el?.classList?.remove('selected')
    }
    this.$store.commit('setCurrentIRSelection', { id: undefined })

    const spans = this.$refs.llvm.querySelectorAll('.llvm[id]')
    for (const span of spans) {
      span.addEventListener('mouseover', (e) => {
        e.stopImmediatePropagation()
        span.classList.add('hover')
        for (const prov of this.$store.state.currentProvenanceMap[span.id] ?? []) {
          const provenanceSpan = document.getElementById(prov)
          provenanceSpan?.classList?.add('hover')
        }
      })
      span.addEventListener('mouseleave', (e) => {
        span.classList.remove('hover')
        for (const prov of this.$store.state.currentProvenanceMap[span.id] ?? []) {
          const provenanceSpan = document.getElementById(prov)
          provenanceSpan?.classList?.remove('hover')
        }
      })
      span.addEventListener('click', (e) => {
        for (const el of document.querySelectorAll('.selected') ?? []) {
          el?.classList?.remove('selected')
        }
        span.classList.add('selected')
        for (const el of document.querySelectorAll(`[data-provenance="${span.id}"]`) ?? []) {
          el?.classList?.add('selected')
        }
        this.$store.commit('setCurrentIRSelection', { id: span.textContent.match(/^(\d+)\|(\d+)/m)[1] })
      })
    }
  }
}
</script>

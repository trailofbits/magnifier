<template>
  <div>
    <h1 class="main-label text-lg my-2">
      Rellic Decompiled Result
    </h1>
    <!-- <vue-code-highlight class="overflow-y-scroll">
      <pre >{{currentFuncDecompiledContent}}</pre>
    </vue-code-highlight> -->
    <div class="overflow-scroll h-full">
      <pre ref="clang" v-html="$store.state.currentFuncDecompiledContent" />
    </div>
  </div>
</template>

<script>
export default {
  mounted () {
    this.$parent.$el.style.overflow = 'scroll'
  },
  updated () {
    const spans = this.$refs.clang.querySelectorAll('.clang[id]')
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
        e.stopImmediatePropagation()
        for (const el of document.querySelectorAll('.selected') ?? []) {
          el?.classList?.remove('selected')
        }

        const provenances = this.$store.state.currentProvenanceMap[span.id] ?? []
        if (provenances.length <= 0) { return }

        const provenanceSpan = document.getElementById(provenances[0])
        if (provenanceSpan === null) { return }

        provenanceSpan.classList.add('selected')
        span.classList.add('selected')
        this.$store.commit('setCurrentIRSelection', { id: provenanceSpan.textContent.match(/^(\d+)\|(\d+)/m)[1] })
      })
    }
  }
}
</script>

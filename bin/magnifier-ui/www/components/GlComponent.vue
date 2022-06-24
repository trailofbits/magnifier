<template>
  <div ref="GLComponent" style="position: absolute; overflow: hidden">
    <slot />
  </div>
</template>

<script>

const numberToPixels = (value) => {
  return value.toString(10) + 'px'
}

export default {
  methods: {
    setPosAndSize (left, top, width, height) {
      if (this.$refs.GLComponent) {
        const el = this.$refs.GLComponent
        el.style.left = numberToPixels(left)
        el.style.top = numberToPixels(top)
        el.style.width = numberToPixels(width)
        el.style.height = numberToPixels(height)
      }
    },

    setVisibility (visible) {
      if (this.$refs.GLComponent) {
        const el = this.$refs.GLComponent
        if (visible) {
          el.style.display = ''
        } else {
          el.style.display = 'none'
        }
      }
    },

    setZIndex (value) {
      if (this.$refs.GLComponent) {
        const el = this.$refs.GLComponent
        el.style.zIndex = value
      }
    }
  }
}
</script>

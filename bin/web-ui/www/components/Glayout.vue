<template>
  <div style="position: relative; width: 100%; height: 100%">
    <div ref="GLRoot" style="position: absolute; width: 100%; height: 100%">
      <!-- Root dom for Golden-Layout manager -->
    </div>
    <div style="position: absolute; width: 100%; height: 100%">
      <gl-component
        v-for="(val, key) in AllComponents"
        :key="key"
        :ref="GlcKeyPrefix + key"
      >
        <component :is="val" />
        <!-- <h1>{{val}} - {{key}}</h1> -->
      </gl-component>
    </div>
  </div>
</template>

<script>
import Vue from 'vue'

import {
  LayoutConfig,
  VirtualLayout
} from 'golden-layout'

import GlComponent from '@/components/GlComponent.vue'

export default {
  name: 'Glayout',
  components: {
    GlComponent
  },
  props: {
    glcPath: String
  },
  data () {
    return {
      GLayout: {},
      GlcKeyPrefix: 'glc_',
      AllComponents: {},
      MapComponents: new Map(),
      CurIndex: 0,
      UnusedIndexes: [],
      GlBoundingClientRect: null
    }
  },
  mounted () {
    if (this.$refs.GLRoot == null) {
      throw new Error('Golden Layout can\'t find the root DOM!')
    }

    const onResize = () => {
      const dom = this.$refs.GLRoot
      const width = dom ? dom.offsetWidth : 0
      const height = dom ? dom.offsetHeight : 0
      this.GLayout.setSize(width, height)
    }

    window.addEventListener('resize', onResize, { passive: true })

    const handleBeforeVirtualRectingEvent = (count) => {
      this.GlBoundingClientRect = (
        this.$refs.GLRoot
      ).getBoundingClientRect()
    }

    const handleContainerVirtualRectingRequiredEvent = (
      container,
      width,
      height
    ) => {
      const component = this.MapComponents.get(container)
      if (!component || !component?.glc) {
        throw new Error(
          'handleContainerVirtualRectingRequiredEvent: Component not found'
        )
      }

      const containerBoundingClientRect =
        container.element.getBoundingClientRect()
      const left = containerBoundingClientRect.left - this.GlBoundingClientRect.left
      const top = containerBoundingClientRect.top - this.GlBoundingClientRect.top
      component.glc.setPosAndSize(left, top, width, height)
    }

    const handleContainerVirtualVisibilityChangeRequiredEvent = (
      container,
      visible
    ) => {
      const component = this.MapComponents.get(container)
      if (!component || !component?.glc) {
        throw new Error(
          'handleContainerVirtualVisibilityChangeRequiredEvent: Component not found'
        )
      }
      component.glc.setVisibility(visible)
    }

    const handleContainerVirtualZIndexChangeRequiredEvent = (
      container,
      logicalZIndex,
      defaultZIndex
    ) => {
      const component = this.MapComponents.get(container)
      if (!component || !component?.glc) {
        throw new Error(
          'handleContainerVirtualZIndexChangeRequiredEvent: Component not found'
        )
      }

      component.glc.setZIndex(defaultZIndex)
    }

    const bindComponentEventListener = (
      container,
      itemConfig
    ) => {
      let refId = -1
      if (itemConfig && itemConfig.componentState) {
        refId = (itemConfig.componentState).refId
      } else {
        throw new Error(
          'bindComponentEventListener: component\'s ref id is required'
        )
      }

      const ref = this.GlcKeyPrefix + refId
      const component = this.$refs[ref][0]

      this.MapComponents.set(container, { refId, glc: component })

      container.virtualRectingRequiredEvent = (container, width, height) =>
        handleContainerVirtualRectingRequiredEvent(container, width, height)

      container.virtualVisibilityChangeRequiredEvent = (container, visible) =>
        handleContainerVirtualVisibilityChangeRequiredEvent(container, visible)

      container.virtualZIndexChangeRequiredEvent = (
        container,
        logicalZIndex,
        defaultZIndex
      ) =>
        handleContainerVirtualZIndexChangeRequiredEvent(
          container,
          logicalZIndex,
          defaultZIndex
        )

      return {
        component,
        virtual: true
      }
    }

    const unbindComponentEventListener = (
      container
    ) => {
      const component = this.MapComponents.get(container)
      if (!component || !component?.glc) {
        throw new Error('handleUnbindComponentEvent: Component not found')
      }

      this.MapComponents.delete(container)
      // this.AllComponents.delete(component.refId)
      Vue.delete(this.AllComponents, component.refId)
      this.UnusedIndexes.push(component.refId)
    }
    this.GLayout = new VirtualLayout(
      this.$refs.GLRoot,
      bindComponentEventListener,
      unbindComponentEventListener
    )

    this.GLayout.beforeVirtualRectingEvent = handleBeforeVirtualRectingEvent
  },
  methods: {
    addComponent (componentType, title) {
      const glc = Vue.component(componentType, () => import(this.glcPath + componentType + '.vue'))

      let index = this.CurIndex
      if (this.UnusedIndexes.length > 0) {
        index = this.UnusedIndexes.pop()
      } else {
        this.CurIndex++
      }

      // this.AllComponents.set(index, glc)
      Vue.set(this.AllComponents, index, glc)

      return index
    },

    async addGLComponent (componentType, title) {
      if (componentType.length === 0) {
        throw new Error('addGLComponent: Component\'s type is empty')
      }

      const index = this.addComponent(componentType, title)

      await Vue.nextTick()

      this.GLayout.addComponent(componentType, { refId: index }, title)
    },

    async loadGLLayout (
      layoutConfig
    ) {
      this.GLayout.clear()
      // this.AllComponents.clear()
      this.AllComponents = {}

      const config = (
        layoutConfig.resolved
          ? LayoutConfig.fromResolved(layoutConfig)
          : layoutConfig
      )

      const contents = [config.root.content]

      let index = 0
      while (contents.length > 0) {
        const content = contents.shift()
        for (const itemConfig of content) {
          if (itemConfig.type === 'component') {
            index = this.addComponent(
              itemConfig.componentType,
              itemConfig.title
            )
            if (typeof itemConfig.componentState === 'object') {
              itemConfig.componentState.refId = index
            } else {
              itemConfig.componentState = { refId: index }
            }
          } else if (itemConfig.content.length > 0) {
            contents.push(itemConfig.content)
          }
        }
      }

      await Vue.nextTick() // wait 1 tick for vue to add the dom

      this.GLayout.loadLayout(config)
    },

    getLayoutConfig () {
      return this.GLayout.saveLayout()
    }
  }
}

</script>

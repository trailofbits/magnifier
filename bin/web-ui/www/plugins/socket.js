export default ({ app }, inject) => {
  // Inject $hello(msg) in Vue, context and store.
  let socket
  // console.log(app)
  inject('socket', {
    init () {
      this._socket = new WebSocket('ws://localhost:9001/ws')
      // Connection opened
      this._socket.addEventListener('open', function (event) {
        console.log('socket opened!')
        this.connectionResolvers.forEach(r => r.resolve())
      }.bind(this))

      // Listen for messages
      this._socket.addEventListener('message', function (event) {
        const data = JSON.parse(event.data)
        // console.log(data)

        if (data.id && this.receiveResolvers[data.id]) {
          this.receiveResolvers[data.id].resolve(data)
          delete this.receiveResolvers[data.id]
          return
        }

        if (data.message) {
          console.log('Message from server ', data.message)
          // app.store.dispatch('updateFuncs')
          return
        }
        app.store.dispatch('parseWsData', data)
      }.bind(this))
    },

    checkConnection () {
      return new Promise((resolve, reject) => {
        if (this._socket.readyState === WebSocket.OPEN) {
          resolve()
        } else {
          this.connectionResolvers.push({ resolve, reject })
        }
      })
    },

    async send (payload) {
      await this.checkConnection()
      const id = this.packetId
      this.packetId += 1

      this._socket.send(JSON.stringify({
        id,
        ...payload
      }))

      return new Promise((resolve, reject) => {
        this.receiveResolvers[id] = { resolve, reject }
      })
    },

    _socket: socket,
    connectionResolvers: [],
    receiveResolvers: {},
    packetId: 1
  })
}

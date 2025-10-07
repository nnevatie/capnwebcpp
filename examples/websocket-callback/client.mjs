import { newWebSocketRpcSession, RpcTarget } from "../../dist/index.js";

class ClientApi extends RpcTarget {
  greet(name) {
    console.log("ClientApi.greet invoked with:", name);
    return `Hello, ${name}! (from client)`;
  }

  get version() {
    return "1.2.3";
  }
}

const api = newWebSocketRpcSession("ws://127.0.0.1:8000/api");

// Register our client API with the server. The server will then call
// clientApi.greet("from server") and clientApi.version via server->client calls.
const clientApi = new ClientApi();
const res = await api.register(clientApi);
console.log("Server register() returned:", res);


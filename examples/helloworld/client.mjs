import { newWebSocketRpcSession } from "../../dist/index.js";

// One-line setup.
let api = newWebSocketRpcSession("ws://127.0.0.1:8000/api");

// Call a method on the server!
let result = await api.hello("World");

console.log(result);

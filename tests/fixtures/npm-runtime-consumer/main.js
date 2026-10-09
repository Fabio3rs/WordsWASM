import {checkEngine} from "./checks.js";
window.consumerResult = {state: "running"};
try {
  await checkEngine();
  await new Promise((resolve, reject) => {
    const worker = new Worker(new URL("./worker.js", import.meta.url), {type: "module"});
    const timeout = setTimeout(() => {
      worker.terminate();
      reject(new Error("Worker timed out"));
    }, 60000);
    const finish = (error) => {
      clearTimeout(timeout);
      worker.terminate();
      error ? reject(error) : resolve();
    };
    worker.onmessage = ({data}) => finish(data.result === "passed" ? null : new Error(data.error));
    worker.onerror = (event) => finish(new Error(event.message));
  });
  window.consumerResult = {state: "passed"};
} catch (error) {
  window.consumerResult = {state: "failed", error: error.stack || String(error)};
}

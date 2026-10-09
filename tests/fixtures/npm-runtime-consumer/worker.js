import {checkEngine} from "./checks.js";
checkEngine().then(
  (result) => postMessage({result}),
  (error) => postMessage({error: error.stack || String(error)}),
);

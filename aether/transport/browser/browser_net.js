/**
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Emscripten JS library for browser WebSocket and HTTP tunnel I/O.
 * Generation is passed as i32 (wasm dynCall cannot use "j" / i64).
 * Link with: --js-library aether/transport/browser/browser_net.js
 */

mergeInto(LibraryManager.library, {
  $AeBrowserNet__postset: 'AeBrowserNet = { nextId: 1, sockets: {}, sessions: {} };',
  $AeBrowserNet: {},

  ae_browser_ws_open__deps: ['$AeBrowserNet'],
  ae_browser_ws_open: function(urlPtr, userData, generation,
                               onOpen, onMessage, onClose, onError) {
    var url = UTF8ToString(urlPtr);
    var id = AeBrowserNet.nextId++;
    var gen = generation | 0;
    var entry = {
      id: id,
      userData: userData,
      generation: gen,
      ws: null,
      closed: false
    };
    AeBrowserNet.sockets[id] = entry;
    try {
      var ws = new WebSocket(url);
      ws.binaryType = 'arraybuffer';
      entry.ws = ws;
      ws.onopen = function() {
        if (entry.closed) return;
        {{{ makeDynCall('vii', 'onOpen') }}}(userData, gen);
      };
      ws.onmessage = function(ev) {
        if (entry.closed) return;
        if (typeof ev.data === 'string') {
          {{{ makeDynCall('vii', 'onError') }}}(userData, gen);
          return;
        }
        var u8 = new Uint8Array(ev.data);
        var ptr = _malloc(u8.length);
        if (!ptr) {
          {{{ makeDynCall('vii', 'onError') }}}(userData, gen);
          return;
        }
        HEAPU8.set(u8, ptr);
        {{{ makeDynCall('viiii', 'onMessage') }}}(userData, gen, ptr, u8.length);
        _free(ptr);
      };
      ws.onclose = function() {
        if (entry.closed) return;
        entry.closed = true;
        {{{ makeDynCall('vii', 'onClose') }}}(userData, gen);
        delete AeBrowserNet.sockets[id];
      };
      ws.onerror = function() {
        if (entry.closed) return;
        {{{ makeDynCall('vii', 'onError') }}}(userData, gen);
      };
    } catch (e) {
      entry.closed = true;
      delete AeBrowserNet.sockets[id];
      {{{ makeDynCall('vii', 'onError') }}}(userData, gen);
      return 0;
    }
    return id;
  },

  ae_browser_ws_send__deps: ['$AeBrowserNet'],
  ae_browser_ws_send: function(handle, dataPtr, size) {
    var entry = AeBrowserNet.sockets[handle];
    if (!entry || !entry.ws || entry.ws.readyState !== 1) {
      return -1;
    }
    try {
      var slice = HEAPU8.buffer.slice(dataPtr, dataPtr + size);
      entry.ws.send(slice);
      return size;
    } catch (e) {
      return -1;
    }
  },

  ae_browser_ws_buffered_amount__deps: ['$AeBrowserNet'],
  ae_browser_ws_buffered_amount: function(handle) {
    var entry = AeBrowserNet.sockets[handle];
    if (!entry || !entry.ws) {
      return 0;
    }
    return entry.ws.bufferedAmount | 0;
  },

  ae_browser_ws_close__deps: ['$AeBrowserNet'],
  ae_browser_ws_close: function(handle) {
    var entry = AeBrowserNet.sockets[handle];
    if (!entry) {
      return;
    }
    entry.closed = true;
    try {
      if (entry.ws && entry.ws.readyState < 2) {
        entry.ws.close();
      }
    } catch (e) {}
    delete AeBrowserNet.sockets[handle];
  },

  ae_browser_http_connect__deps: ['$AeBrowserNet'],
  ae_browser_http_connect: function(urlPtr, bodyPtr, bodySize, userData,
                                    generation, onOk, onErr) {
    var url = UTF8ToString(urlPtr);
    var gen = generation | 0;
    var body = (bodyPtr && bodySize > 0)
      ? HEAPU8.slice(bodyPtr, bodyPtr + bodySize)
      : null;
    var id = AeBrowserNet.nextId++;
    AeBrowserNet.sessions[id] = {
      id: id,
      aborted: false,
      receiveCtrl: null,
      userData: userData
    };
    fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: body ? new TextDecoder().decode(body) : '{}'
    }).then(function(resp) {
      if (!resp.ok) {
        throw new Error('connect http ' + resp.status);
      }
      return resp.json();
    }).then(function(obj) {
      var session = (obj && obj.session) ? String(obj.session) : "";
      if (!session) {
        throw new Error('missing session');
      }
      var sess = AeBrowserNet.sessions[id];
      if (!sess || sess.aborted) {
        return;
      }
      sess.sessionId = session;
      var len = lengthBytesUTF8(session) + 1;
      var ptr = _malloc(len);
      stringToUTF8(session, ptr, len);
      {{{ makeDynCall('viii', 'onOk') }}}(userData, gen, ptr);
      _free(ptr);
    }).catch(function() {
      var sess = AeBrowserNet.sessions[id];
      if (!sess || sess.aborted) {
        return;
      }
      {{{ makeDynCall('vii', 'onErr') }}}(userData, gen);
      delete AeBrowserNet.sessions[id];
    });
    return id;
  },

  ae_browser_http_send__deps: ['$AeBrowserNet'],
  ae_browser_http_send: function(handle, urlPtr, dataPtr, size, userData,
                                 generation, onOk, onErr) {
    var sess = AeBrowserNet.sessions[handle];
    if (!sess || sess.aborted) {
      return -1;
    }
    var url = UTF8ToString(urlPtr);
    var gen = generation | 0;
    var body = HEAPU8.slice(dataPtr, dataPtr + size);
    fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: body
    }).then(function(resp) {
      if (!sess || sess.aborted) {
        return;
      }
      if (!resp.ok) {
        throw new Error('send http ' + resp.status);
      }
      {{{ makeDynCall('vii', 'onOk') }}}(userData, gen);
    }).catch(function() {
      if (!sess || sess.aborted) {
        return;
      }
      {{{ makeDynCall('vii', 'onErr') }}}(userData, gen);
    });
    return 0;
  },

  ae_browser_http_receive__deps: ['$AeBrowserNet'],
  ae_browser_http_receive: function(handle, urlPtr, userData, generation,
                                    onData, onErr) {
    var sess = AeBrowserNet.sessions[handle];
    if (!sess || sess.aborted) {
      return -1;
    }
    var url = UTF8ToString(urlPtr);
    var gen = generation | 0;
    var ctrl = (typeof AbortController !== 'undefined')
      ? new AbortController()
      : null;
    sess.receiveCtrl = ctrl;
    fetch(url, {
      method: 'GET',
      signal: ctrl ? ctrl.signal : undefined
    }).then(function(resp) {
      if (!resp.ok) {
        throw new Error('receive http ' + resp.status);
      }
      return resp.arrayBuffer();
    }).then(function(buf) {
      if (!AeBrowserNet.sessions[handle] ||
          AeBrowserNet.sessions[handle].aborted) {
        return;
      }
      var u8 = new Uint8Array(buf);
      if (u8.length === 0) {
        {{{ makeDynCall('viiii', 'onData') }}}(userData, gen, 0, 0);
        return;
      }
      var ptr = _malloc(u8.length);
      HEAPU8.set(u8, ptr);
      {{{ makeDynCall('viiii', 'onData') }}}(userData, gen, ptr, u8.length);
      _free(ptr);
    }).catch(function(err) {
      if (ctrl && err && err.name === 'AbortError') {
        return;
      }
      if (!AeBrowserNet.sessions[handle] ||
          AeBrowserNet.sessions[handle].aborted) {
        return;
      }
      {{{ makeDynCall('vii', 'onErr') }}}(userData, gen);
    });
    return 0;
  },

  ae_browser_http_close__deps: ['$AeBrowserNet'],
  ae_browser_http_close: function(handle, urlPtr) {
    var sess = AeBrowserNet.sessions[handle];
    if (sess) {
      sess.aborted = true;
      try {
        if (sess.receiveCtrl) {
          sess.receiveCtrl.abort();
        }
      } catch (e) {}
      delete AeBrowserNet.sessions[handle];
    }
    if (urlPtr) {
      var url = UTF8ToString(urlPtr);
      try {
        fetch(url, { method: 'POST' }).catch(function() {});
      } catch (e) {}
    }
  }
});

(function() {
  if (typeof globalThis === 'undefined') {
    this.globalThis = this;
  }
  if (typeof window === 'undefined') {
    this.window = this;
  }
  if (typeof self === 'undefined') {
    this.self = this;
  }
  if (typeof console === 'undefined') {
    this.console = { log: function(){}, warn: function(){}, error: function(){} };
  }
})();

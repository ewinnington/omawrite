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
  if (typeof process === 'undefined') {
    this.process = { env: {}, versions: { node: '18.0.0' } };
  }

  var existing = (typeof MathJax === 'undefined' || !MathJax) ? {} : MathJax;
  MathJax = Object.assign({
    loader: { load: ['input/tex-full', 'output/svg'] },
    startup: { typeset: false }
  }, existing);

  this.texToSvg = function(tex, display) {
    try {
      var node = MathJax.tex2svg(tex, { display: !!display });
      var svgNode = node.getElementsByTagName('svg')[0];
      return {
        svg: MathJax.startup.adaptor.outerHTML(svgNode),
        error: ''
      };
    } catch (err) {
      return {
        svg: '',
        error: String(err && err.message ? err.message : err)
      };
    }
  };
})();

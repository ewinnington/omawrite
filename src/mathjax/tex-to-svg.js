const {mathjax} = require('mathjax-full/js/mathjax.js');
const {TeX} = require('mathjax-full/js/input/tex.js');
const {SVG} = require('mathjax-full/js/output/svg.js');
const {liteAdaptor} = require('mathjax-full/js/adaptors/liteAdaptor.js');
const {RegisterHTMLHandler} = require('mathjax-full/js/handlers/html.js');
const {AllPackages} = require('mathjax-full/js/input/tex/AllPackages.js');

const packages = AllPackages.filter((name) => name !== 'bussproofs');
const adaptor = liteAdaptor();
RegisterHTMLHandler(adaptor);

const html = mathjax.document('', {
  InputJax: new TeX({packages}),
  OutputJax: new SVG({fontCache: 'none', merrorInheritFont: true})
});

function texToSvg(tex, display) {
  try {
    const node = html.convert(String(tex ?? ''), {
      display: !!display,
      em: 16,
      ex: 8,
      containerWidth: 80 * 16
    });
    const svgNode = adaptor.firstChild(node);
    if (!svgNode) {
      return {svg: '', error: 'MathJax returned no SVG node'};
    }
    return {svg: adaptor.outerHTML(svgNode), error: ''};
  } catch (err) {
    return {
      svg: '',
      error: String(err && err.message ? err.message : err)
    };
  }
}

globalThis.texToSvg = texToSvg;
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {texToSvg};
}

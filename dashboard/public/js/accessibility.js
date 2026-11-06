let opener=null;
export function openDrawer(drawer){opener=document.activeElement;document.body.classList.add('drawer-open');document.querySelector('#drawerBackdrop').hidden=false;drawer.hidden=false;requestAnimationFrame(()=>drawer.querySelector('button,input,[tabindex]')?.focus());}
export function closeDrawers(){for(const drawer of document.querySelectorAll('.drawer'))drawer.hidden=true;document.querySelector('#drawerBackdrop').hidden=true;document.body.classList.remove('drawer-open');opener?.focus();}
export function installFocusHandling(){
  document.addEventListener('keydown',event=>{const drawer=[...document.querySelectorAll('.drawer')].find(d=>!d.hidden);if(event.key==='Escape'){closeDrawers();return;}if(event.key!=='Tab'||!drawer)return;const nodes=[...drawer.querySelectorAll('button:not(:disabled),input:not(:disabled),[tabindex]:not([tabindex="-1"])')];if(!nodes.length)return;const first=nodes[0],last=nodes.at(-1);if(event.shiftKey&&document.activeElement===first){event.preventDefault();last.focus();}else if(!event.shiftKey&&document.activeElement===last){event.preventDefault();first.focus();}});
}

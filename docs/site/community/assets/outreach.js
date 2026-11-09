"use strict";
const filters = [...document.querySelectorAll("[data-filter]")];
const cards = [...document.querySelectorAll(".destination")];
const count = document.querySelector(".filter-count");
filters.forEach(button => button.addEventListener("click", () => {
 const group = button.dataset.filter;
 filters.forEach(other => {const selected = other === button; other.classList.toggle("active", selected); other.setAttribute("aria-pressed", String(selected));});
 cards.forEach(card => {card.hidden = group !== "all" && card.dataset.group !== group;});
 const visible = cards.filter(card => !card.hidden).length;
 count.textContent = `Showing ${visible} destination${visible === 1 ? "" : "s"}`;
}));
// Anchor navigation also restores a destination hidden by the selected filter.
window.addEventListener("hashchange", () => {const target = document.getElementById(location.hash.slice(1));if(target?.classList.contains("destination") && target.hidden) filters[0].click();});

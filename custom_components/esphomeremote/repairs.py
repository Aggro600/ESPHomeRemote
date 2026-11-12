"""Behebbare Reparatur-Hinweise (2026-11-12): HA ruft async_create_fix_flow, wenn der Nutzer auf "Beheben" tippt."""
from __future__ import annotations

from homeassistant import data_entry_flow
from homeassistant.components.repairs import ConfirmRepairFlow, RepairsFlow
from homeassistant.core import HomeAssistant


class AlteDateienFlow(RepairsFlow):
    """old_files: alte ESPHomeRemote-Dateien ins Archiv des ESPHome-Ordners verschieben (wie der Knopf im Konfigurator)."""

    async def async_step_init(self, user_input=None) -> data_entry_flow.FlowResult:
        return await self.async_step_confirm()

    async def async_step_confirm(self, user_input=None) -> data_entry_flow.FlowResult:
        from .firmware import altlasten, altlasten_wegraeumen
        if user_input is not None:
            ordner = await self.hass.async_add_executor_job(altlasten_wegraeumen)
            from homeassistant.helpers import issue_registry as ir
            ir.async_delete_issue(self.hass, "esphomeremote", "old_files")
            return self.async_create_entry(data={"archiv": ordner})
        dateien = await self.hass.async_add_executor_job(altlasten)
        return self.async_show_form(step_id="confirm", description_placeholders={"dateien": ", ".join(dateien) or "–"})


async def async_create_fix_flow(hass: HomeAssistant, issue_id: str, data: dict | None) -> RepairsFlow:
    if issue_id == "old_files":
        return AlteDateienFlow()
    return ConfirmRepairFlow()

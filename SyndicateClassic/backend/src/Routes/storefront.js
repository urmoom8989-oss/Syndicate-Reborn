import {catalogForClient} from '../Utils/syndicateLegacy111Catalog.js';
import {stwDevEnabled} from '../Utils/syndicateStwDev.js';
import {stwStorefronts} from '../Utils/syndicateStwMcp.js';
import express from "express";
const app = express.Router();
import Profile from "../User/Mongodb/Schema/profiles.js";
import Friends from "../User/Mongodb/Schema/friends.js";
import Utils from "../Utils/Utils.js";
import { verifyToken } from "../User/tokenManager/tokenVerify.js";
import fs from "fs";
import { vbucksBulkOffers, vbucksPrices, noteCheckoutAccount } from "../Utils/syndicateVbucksStore.js";

const keychain = JSON.parse(
  fs.readFileSync("./src/local/KeyChain/keychain.json", "utf8")
);

function readCatalogConfig(userAgent) {
  return Utils.getStoreCatalog(userAgent);
}

app.get("/fortnite/api/storefront/v2/catalog", (req, res) => {
  if (!req.headers["user-agent"]) {
    return res.status(400).end();
  }
  if (req.headers["user-agent"].includes("2870186")) {
    return res.status(404).end();
  }

  try {
    const catalog = readCatalogConfig(req.headers["user-agent"]);
    const client = catalogForClient(catalog, req.headers["user-agent"]);
    // Save the World on: its llama store and event heroes too (LawinServer's storefronts, syndicateStwMcp.js).
    if (stwDevEnabled(req.headers["user-agent"])) {
      try {
        const have = new Set((client.storefronts || []).map(store => store.name));
        return res.json({ ...client, storefronts: [...(client.storefronts || []), ...stwStorefronts().filter(store => !have.has(store.name))] });
      } catch (error) { console.log("[Syndicate STW] no llama store:", error.message); }
    }
    res.json(client);
  } catch (err) {
    console.log("Failed to load catalog_config.json:", err);
    res.status(500).json({ error: "Internal server error" });
  }
});
app.get(
  "/fortnite/api/storefront/v2/gift/check_eligibility/recipient/:recipientId/offer/:offerId",
  verifyToken,
  async (req, res) => {
    const findOfferId = Utils.getOfferID(req.params.offerId,req.headers["user-agent"]);
    if (!findOfferId) {
      return Utils.createError(
        "errors.com.epicgames.fortnite.id_invalid",
        `Offer ID (id: "${req.params.offerId}") not found`,
        [req.params.offerId],
        16027,
        undefined,
        400,
        res
      );
    }
    const sender = await Friends.findOne({
      accountId: req.user.accountId,
    }).lean();
    const acceptedFriend = sender.list.accepted.find(
      (i) => i.accountId == req.params.recipientId
    );
    if (!acceptedFriend && req.params.recipientId != req.user.accountId) {
      return Utils.createError(
        "errors.com.epicgames.friends.no_relationship",
        `User ${req.user.accountId} is not friends with ${req.params.recipientId}`,
        [req.user.accountId, req.params.recipientId],
        28004,
        undefined,
        403,
        res
      );
    }
    const profiles = await Profile.findOne({
      accountId: req.params.recipientId,
    });
    const athena = profiles.profiles["athena"];
    for (const itemGrant of findOfferId.offerId.itemGrants) {
      if (!athena.items || typeof athena.items !== "object") {
        return Utils.createError(
          "errors.com.epicgames.modules.gamesubcatalog.purchase_not_allowed",
          `Could not purchase catalog offer ${findOfferId.offerId.devName}, item ${itemGrant.templateId} as 'items' is not an object`,
          [findOfferId.offerId.devName, itemGrant.templateId],
          28004,
          undefined,
          403,
          res
        );
      }
      const templateIdLowerCase = itemGrant.templateId.toLowerCase();
      const itemKeys = Object.keys(athena.items);
      if (
        itemKeys.some(
          (itemKey) => itemKey.toLowerCase() === templateIdLowerCase
        )
      ) {
        return Utils.createError(
          "errors.com.epicgames.modules.gamesubcatalog.purchase_not_allowed",
          `Could not purchase catalog offer ${findOfferId.offerId.devName}, item ${itemGrant.templateId}`,
          [findOfferId.offerId.devName, itemGrant.templateId],
          28004,
          undefined,
          403,
          res
        );
      }
    }
    res.json({
      price: findOfferId.offerId.prices[0],
      items: findOfferId.offerId.itemGrants,
    });
  }
);
app.get("/fortnite/api/storefront/v2/keychain", (req, res) => {
  res.json(keychain);
});
app.get("/catalog/api/shared/bulk/offers", (req, res) => {
  // The STORE tab's V-Bucks packs (syndicateVbucksStore.js): the store entry of each pack the game asks for.
  const packs = vbucksBulkOffers(req.query.id);
  // EXPERIMENT (dev builds only): answer the commerce lookup for the placeholder Rogue Agent Starter Pack,
  // so the STORE tab has something to render. Logs the request so we can see what the client actually asks for.
  if (process.env.TILTED_DEVKIT === "1") {
    console.log("[Syndicate] bulk/offers query:", JSON.stringify(req.query));
    return res.json({ ...Utils.devBulkOffers(), ...packs });
  }
  res.json(packs);
});

// EXPERIMENT (dev builds only): the price lookup the STORE tab makes after bulk/offers. Without this the
// placeholder offer has no price and the tab drops it ("No Offers Available"). Returns a price for each
// requested line offer. Logs the body so we can match the real client's exact shape.
function devPriceEngine(req, res) {
  // The V-Bucks packs' prices ($9.99 to $99.99, syndicateVbucksStore.js). Note the account: the checkout page
  // adds the V-Bucks to the account playing on this PC.
  noteCheckoutAccount(req.socket?.remoteAddress, req.body?.accountId);
  // EXPERIMENT (dev builds only): the placeholder Rogue Agent Pack is priced too ($4.99). Without a price the
  // STORE tab drops an offer ("No Offers Available"). Logs the body so we can match the real client's exact shape.
  const dev = process.env.TILTED_DEVKIT === "1";
  if (dev) console.log("[Syndicate] priceengine body:", JSON.stringify(req.body));
  res.json(vbucksPrices(req.body, dev ? 499 : 0) || {});
}
app.post("/priceengine/api/shared/offers/price", devPriceEngine);
app.post("/priceengine/api/shared/offer/price", devPriceEngine);
export default app;
//# sourceMappingURL=storefront.js.map
